package com.vrushabhhirap.hidimbistown

import android.net.Uri
import android.os.Bundle
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.vrushabhhirap.hidimbistown.native.NativeBridge

/**
 * The town map. It is deliberately not a game: every landmark is a row showing
 * which realms are locked and which have been opened, and the only way to open
 * one is the component behind it (ContentProvider, BroadcastReceiver, bound
 * Service or exported Activity).
 *
 * The visible order is the DECOY graph. The real order has to be derived.
 */
class MainActivity : AppCompatActivity() {

    companion object {
        val LANDMARKS = listOf(
            "Forest" to 17, "Temple" to 4, "Cave" to 29,
            "Market" to 13, "Palace" to 8, "Lake" to 31,
            "Mosaic of the Nine Wells" to 6, "The Scriptorium" to 33,
            "The Drum Tower" to 22, "The Courier's Ledger" to 14,
            "The Granary" to 25, "The Observatory" to 19,
            "The Keeper's Whistle" to 11, "The Vault" to 1
        )
    }

    private val state = mutableMapOf<String, String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Initialise the native security & anti-analysis engine (stages 1-2)
        NativeBridge.initNative()
    }

    override fun onResume() {
        super.onResume()
        val list = findViewById<LinearLayout>(R.id.landmark_list)
        if (list == null) { renderFallback(); return }
        list.removeAllViews()

        LANDMARKS.forEach { (name, node) ->
            val status = state[name] ?: probe(name, node)
            state[name] = status
            val row = TextView(this).apply {
                text = "%2d  %-26s %s".format(node, name, status)
                textSize = 13f
                setPadding(8, 8, 8, 8)
            }
            list.addView(row)
        }

        findViewById<TextView>(R.id.hint)?.text =
            "The map lies. The realms answer only to the components behind them."
    }

    private fun renderFallback() {
        setContentView(TextView(this).apply { text = "Hidimbi's Town" })
    }

    /**
     * Ask each component whether it will speak. This is the in-app feedback:
     * a landmark shows LOCKED until the chain that opens it has been walked,
     * and shows OPEN (or the direction it hands over) afterwards.
     */
    private fun probe(name: String, node: Int): String {
        return try {
            when (node) {
                17, 29 -> {
                    val c = contentResolver.query(
                        Uri.parse("content://com.vrushabhhirap.hidimbistown.provider/cave"),
                        null, null, null, null)
                    val seen = c?.let { if (it.moveToFirst()) it.getString(0) else "" }
                    c?.close()
                    if (seen?.contains("DECOY", true) == true) "answers with a decoy" else "sealed"
                }
                4, 8, 13, 31 -> "exported (see decode)"
                6 -> "assets/mosaic_wells.bin (2304 bytes)"
                else -> "under the town"
            }
        } catch (e: Exception) {
            "unreachable"
        }
    }
}
