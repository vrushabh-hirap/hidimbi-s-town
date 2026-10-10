package com.vrushabhhirap.hidimbistown

import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.os.Parcel
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

class TownService : Service() {

    private val binder = object : Binder() {
        override fun onTransact(code: Int, data: Parcel, reply: Parcel?, flags: Int): Boolean {
            if (code == 0x00000001) { // TRANSACTION_requestPalaceFragment
                data.enforceInterface("com.vrushabhhirap.hidimbistown.ITownService")
                val authProof = data.readString() ?: ""
                val nodeId = data.readInt()

                // Calculate expected HMAC-SHA256(key = K1||K2, message = N1||N2)
                val k1 = hexToBytes("4a8f9c2d1e0b5a3f7c8d9e2a1b0c3d4e")
                val k2 = hexToBytes("3b7e1a9c4f2d0e8b6a5c3e1f9d2a0b4c")
                val key = k1 + k2

                val n1 = hexToBytes("f0e1d2c3b4a5968778695a4b3c2d1e0f")
                val n2 = hexToBytes("112233445566778899aabbccddeeff00")
                val msg = n1 + n2

                val mac = Mac.getInstance("HmacSHA256")
                mac.init(SecretKeySpec(key, "HmacSHA256"))
                val expectedProof = mac.doFinal(msg).joinToString("") { "%02x".format(it) }

                if (nodeId == 8 && authProof.equals(expectedProof, ignoreCase = true)) {
                    reply?.writeNoException()
                    reply?.writeInt(8)  // node_id
                    reply?.writeInt(5)  // dir: Next = (8 + 5) % 37 = 13 (Market)
                    reply?.writeString("c9d8e7f6a5b4c3d2e1f0123456789abc") // k3
                    reply?.writeString("aabbccddeeff00112233445566778899") // n3
                    reply?.writeString("PALACE_GRANTED")
                    return true
                } else {
                    reply?.writeNoException()
                    reply?.writeString("ERR_PALACE_DENIED")
                    return true
                }
            }
            return super.onTransact(code, data, reply, flags)
        }
    }

    override fun onBind(intent: Intent?): IBinder {
        return binder
    }

    private fun hexToBytes(hex: String): ByteArray {
        val result = ByteArray(hex.length / 2)
        for (i in result.indices) {
            result[i] = hex.substring(i * 2, i * 2 + 2).toInt(16).toByte()
        }
        return result
    }
}
