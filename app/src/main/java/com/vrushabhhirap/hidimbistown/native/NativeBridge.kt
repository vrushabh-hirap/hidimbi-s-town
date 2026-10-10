package com.vrushabhhirap.hidimbistown.native

import android.content.res.AssetManager

/**
 * JNI declarations for the "under-town" stages (13-17).
 *
 *   Stage 14  runScriptorium  - Scriptorium VM (custom bytecode)
 *   Stage 15  drumTowerFormat - Drum Tower (format string bug)
 *   Stage 16  parseCourierScroll - Courier's Ledger (protobuf abuse)
 *   Stage 17  granaryQuery    - Granary (TOCTOU on mapped shared prefs)
 */
object NativeBridge {
    init {
        System.loadLibrary("town")
    }

    // --- stages 1-11 ------------------------------------------------------
    external fun initNative()
    external fun queryCaveStore(assetManager: AssetManager, query: String): String
    external fun verifyForestToken(certBytes: ByteArray, token: String): Boolean
    external fun deriveFinalKey(
        n1: ByteArray, n2: ByteArray, n3: ByteArray, n4: ByteArray
    ): ByteArray

    // --- stage 14: the Scriptorium VM ---------------------------------------
    /** Runs a bytecode program against the recovered seed. Returns the 16-byte
     *  `scriptorium_mark` as 32 lowercase hex characters, or an `ERR_*` string. */
    external fun runScriptorium(program: ByteArray, seed: ByteArray, gateWord: Int): String

    // --- stage 15: the Drum Tower -------------------------------------------
    /** `note` is the pilgrim-supplied scroll, `markHex` the Stage-14 mark and
     *  `gateTagHex` the pilgrim's HMAC-SHA256(K1 || K2, mark). */
    external fun drumTowerFormat(note: String, markHex: String, gateTagHex: String): String

    // --- stage 16: the Courier's Ledger -------------------------------------
    /** `scroll` is the raw protobuf payload; `drumKeyHex` is the Stage-15 key. */
    external fun parseCourierScroll(scroll: ByteArray, drumKeyHex: String): String

    // --- stage 17: the Granary ----------------------------------------------
    /** `prefsImage` is the memory-mapped shared-preference image, `pilgrimRune`
     *  the 32-byte HMAC for the current epoch. Returns the granary secret as
     *  hex on a won race, or a status string. */
    external fun granaryQuery(
        prefsImage: ByteArray, pilgrimRune: ByteArray, raceWindowUs: Int,
        scribeRuneHex: String, granarySecretHex: String
    ): String
}
