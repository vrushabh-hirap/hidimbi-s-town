package com.vrushabhhirap.hidimbistown

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Bundle
import java.security.MessageDigest

class TownReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == "com.vrushabhhirap.hidimbistown.TEMPLE_PILGRIMAGE") {
            val pilgrimToken = intent.getStringExtra("pilgrim_token") ?: ""

            // Cryptographic validation of K1 XOR N1
            val k1 = hexToBytes("4a8f9c2d1e0b5a3f7c8d9e2a1b0c3d4e")
            val n1 = hexToBytes("f0e1d2c3b4a5968778695a4b3c2d1e0f")
            val xored = ByteArray(16) { i -> (k1[i].toInt() xor n1[i].toInt()).toByte() }

            val md = MessageDigest.getInstance("SHA-256")
            val expectedHash = md.digest(xored).joinToString("") { "%02x".format(it) }

            if (pilgrimToken.equals(expectedHash, ignoreCase = true)) {
                // Success: Return K2, N2, Node 4, Dir 4 -> Next = (4 + 4) % 37 = 8 (Palace)
                val resultExtras = Bundle().apply {
                    putInt("node_id", 4)
                    putInt("dir", 4)
                    putString("k2", "3b7e1a9c4f2d0e8b6a5c3e1f9d2a0b4c")
                    putString("n2", "112233445566778899aabbccddeeff00")
                    putString("status", "SANCTUARY_UNLOCKED")
                }
                setResult(1337, "TEMPLE_BLESSING", resultExtras)
            } else {
                val errorExtras = Bundle().apply {
                    putString("status", "UNHOLY_OFFERING")
                }
                setResult(403, "TEMPLE_REJECTED", errorExtras)
            }
        }
    }

    private fun hexToBytes(hex: String): ByteArray {
        val result = ByteArray(hex.length / 2)
        for (i in result.indices) {
            result[i] = hex.substring(i * 2, i * 2 + 2).toInt(16).toByte()
        }
        return result
    }
}
