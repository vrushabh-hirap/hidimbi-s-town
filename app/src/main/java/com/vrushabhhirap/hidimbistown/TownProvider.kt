package com.vrushabhhirap.hidimbistown

import android.content.ContentProvider
import android.content.ContentValues
import android.content.pm.PackageManager
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import com.vrushabhhirap.hidimbistown.native.NativeBridge
import java.io.File
import java.io.RandomAccessFile

/**
 * Dimbapura's ContentProvider.
 *
 * Paths:
 *   /forest   stage 3   X-Town-Token verification
 *   /cave     stage 5   native TLV store (offset= & len= exploit)
 *   /mosaic   stage 13  raw steganographic mosaic bytes
 *   /scriptorium stage 14 the Scriptorium VM
 *   /drum     stage 15  the Drum Tower (format string)
 *   /courier  stage 16  the Courier's Ledger (protobuf)
 *   /granary  stage 17  the Granary (TOCTOU on a mapped page)
 *   /granary/advance    rewrites the mapped epoch from a second process
 */
class TownProvider : ContentProvider() {

    companion object {
        const val GRANARY_PREFS = "granary_ledger.img"
        const val GRANARY_EPOCH_OFFSET = 64L
        const val GRANARY_PREFS_SIZE = 4096
    }

    override fun onCreate(): Boolean {
        NativeBridge.initNative()
        return true
    }

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?
    ): Cursor? {
        val path = uri.path ?: return null

        // ------------------------------------------------------------------
        // Stage 3 - Forest
        // ------------------------------------------------------------------
        if (path == "/forest") {
            val token = uri.getQueryParameter("token") ?: selection
            if (token == null) {
                return reply("DENIED", "Forest requires valid X-Town-Token")
            }
            val certBytes = signingCert()
            val valid = NativeBridge.verifyForestToken(certBytes, token)
            val cursor = MatrixCursor(arrayOf("node_id", "dir", "status"))
            if (valid) {
                // True Forest Node: ID 17, direction 12 -> (17 + 12) % 37 = 29 (Cave)
                cursor.addRow(arrayOf<Any>(17, 12, "OPEN"))
            } else {
                cursor.addRow(arrayOf<Any>(0, 0, "INVALID_TOKEN"))
            }
            return cursor
        }

        // ------------------------------------------------------------------
        // Stage 5 - Cave
        // ------------------------------------------------------------------
        if (path == "/cave") {
            val queryParam = uri.query ?: selection ?: ""
            val assets = context?.assets ?: return reply("response", "ERR_CONTEXT")
            val result = NativeBridge.queryCaveStore(assets, queryParam)
            return reply("response", result)
        }

        // ------------------------------------------------------------------
        // Stage 13 - the Mosaic of the Nine Wells
        // ------------------------------------------------------------------
        if (path == "/mosaic") {
            val bytes = context?.assets?.open("mosaic_wells.bin")?.use { it.readBytes() }
                ?: return reply("status", "ERR_NO_MOSAIC")
            val hex = bytes.joinToString("") { "%02x".format(it) }
            val cursor = MatrixCursor(arrayOf("tiles", "tile_bytes", "mosaic_hex"))
            cursor.addRow(arrayOf<Any>(9, 256, hex))
            return cursor
        }

        // ------------------------------------------------------------------
        // Stage 14 - the Scriptorium VM
        //   /scriptorium?program=<hex>&seed=<hex>&gate=<u32>
        // ------------------------------------------------------------------
        if (path == "/scriptorium") {
            val programHex = uri.getQueryParameter("program") ?: selection
                ?: return reply("status", "ERR_NO_PROGRAM")
            val seedHex = uri.getQueryParameter("seed")
                ?: return reply("status", "ERR_NO_SEED")
            val gate = (uri.getQueryParameter("gate") ?: "0").toIntOrNull() ?: 0
            val program = hexToBytes(programHex) ?: return reply("status", "ERR_BAD_PROGRAM")
            val seed = hexToBytes(seedHex) ?: return reply("status", "ERR_BAD_SEED")
            return reply("mark", NativeBridge.runScriptorium(program, seed, gate))
        }

        // ------------------------------------------------------------------
        // Stage 15 - the Drum Tower (format-string bug)
        //   /drum?note=<scroll>&mark=<hex>&gate=<hex>
        // ------------------------------------------------------------------
        if (path == "/drum") {
            val note = uri.getQueryParameter("note") ?: selection
                ?: return reply("log", "ERR_NO_NOTE")
            val mark = uri.getQueryParameter("mark")
                ?: return reply("log", "ERR_NO_MARK")
            val gateTag = uri.getQueryParameter("gate")
                ?: return reply("log", "ERR_NO_GATE_TAG")
            return reply("log", NativeBridge.drumTowerFormat(note, mark, gateTag))
        }

        // ------------------------------------------------------------------
        // Stage 16 - the Courier's Ledger (protobuf wire-type abuse)
        //   /courier?scroll=<b64>&key=<hex drum key>
        // ------------------------------------------------------------------
        if (path == "/courier") {
            val b64 = uri.getQueryParameter("scroll") ?: selection
                ?: return reply("echo", "ERR_NO_SCROLL")
            val keyHex = uri.getQueryParameter("key")
                ?: return reply("echo", "ERR_NO_DRUM_KEY")
            try {
                val scroll = android.util.Base64.decode(b64, android.util.Base64.DEFAULT)
                return reply("echo", NativeBridge.parseCourierScroll(scroll, keyHex))
            } catch (e: IllegalArgumentException) {
                return reply("echo", "ERR_BAD_SCROLL_B64")
            }
        }

        // ------------------------------------------------------------------
        // Stage 17 - the Granary (TOCTOU on a memory-mapped page)
        //   /granary?epoch=<u64>&rune=<hex>&scribe=<hex>&secret=<hex>
        //   /granary/advance?epoch=<u64>   (second process re-maps the page)
        // ------------------------------------------------------------------
        if (path == "/granary/advance") {
            val next = (uri.getQueryParameter("epoch") ?: "1").toLongOrNull() ?: 1L
            val prefs = granaryPrefsFile() ?: return reply("status", "ERR_NO_PREFS")
            val mapped = mapPrefs(prefs)
            mapped.use { ch ->
                val buf = ch.map(java.nio.channels.FileChannel.MapMode.READ_WRITE,
                                 0L, GRANARY_PREFS_SIZE.toLong())
                buf.putLong(GRANARY_EPOCH_OFFSET.toInt(), next)
                buf.force()
            }
            return reply("status", "EPOCH=$next")
        }

        if (path == "/granary") {
            val runeHex = uri.getQueryParameter("rune")
                ?: return reply("granary", "ERR_NO_RUNE")
            val scribeHex = uri.getQueryParameter("scribe")
                ?: return reply("granary", "ERR_NO_SCRIBE")
            val secretHex = uri.getQueryParameter("secret")
                ?: return reply("granary", "ERR_NO_SECRET")
            val epoch = (uri.getQueryParameter("epoch") ?: "1").toLongOrNull() ?: 1L
            val window = (uri.getQueryParameter("us") ?: "6000").toIntOrNull() ?: 6000

            val rune = hexToBytes(runeHex) ?: return reply("granary", "ERR_BAD_RUNE")
            // build (or reuse) the 4096-byte mapped image and set the epoch
            val prefs = granaryPrefsFile() ?: return reply("granary", "ERR_NO_PREFS")
            val image = buildGranaryImage(prefs, epoch)
            return reply("granary",
                NativeBridge.granaryQuery(image, rune, window, scribeHex, secretHex))
        }

        return null
    }

    // ------------------------------------------------------------------------
    // Stage 8 - Market (the LAKE_REFLECTION trap and the integer overflow both
    // live in TownActivity, not here)
    // ------------------------------------------------------------------------
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<String>?): Int = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<String>?): Int = 0
    override fun getType(uri: Uri): String = "vnd.android.cursor.dir/vnd.hidimbi.town"

    // ------------------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------------------
    private fun reply(col: String, value: String): Cursor {
        val c = MatrixCursor(arrayOf(col))
        c.addRow(arrayOf<Any>(value))
        return c
    }

    private fun hexToBytes(hex: String): ByteArray? {
        val clean = hex.trim().replace(" ", "").removePrefix("0x")
        if (clean.isEmpty() || clean.length % 2 != 0) return null
        return try {
            ByteArray(clean.length / 2) { clean.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        } catch (e: NumberFormatException) {
            null
        }
    }

    private fun signingCert(): ByteArray = try {
        val pm = context?.packageManager ?: return "hidimbi_debug_signature".toByteArray()
        val pkg = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
            pm.getPackageInfo(context!!.packageName, PackageManager.GET_SIGNING_CERTIFICATES)
                .signingInfo?.apkContentsSigners?.firstOrNull()?.toByteArray()
        } else {
            @Suppress("DEPRECATION")
            pm.getPackageInfo(context!!.packageName, PackageManager.GET_SIGNATURES)
                .signatures?.firstOrNull()?.toByteArray()
        }
        pkg ?: "hidimbi_debug_signature".toByteArray()
    } catch (e: Exception) {
        "hidimbi_debug_signature".toByteArray()
    }

    private fun granaryPrefsFile(): File? {
        val ctx = context ?: return null
        val dir = File(ctx.noBackupFilesDir, "shared_prefs")
        dir.mkdirs()
        return File(dir, GRANARY_PREFS)
    }

    /** A 4096-byte image whose byte 64..71 holds the epoch. */
    private fun buildGranaryImage(file: File, epoch: Long): ByteArray {
        val size = GRANARY_PREFS_SIZE
        if (!file.exists() || file.length() < size) {
            RandomAccessFile(file, "rw").use { raf ->
                raf.setLength(size.toLong())
                raf.seek(0)
                raf.write(ByteArray(size))
            }
        }
        RandomAccessFile(file, "rw").use { raf ->
            raf.seek(GRANARY_EPOCH_OFFSET)
            raf.writeLong(epoch)
        }
        RandomAccessFile(file, "r").use { raf ->
            val buf = ByteArray(size)
            raf.readFully(buf)
            return buf
        }
    }

    private fun mapPrefs(file: File): java.nio.channels.FileChannel =
        RandomAccessFile(file, "rw").channel

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor? = super.openFile(uri, mode)

}
