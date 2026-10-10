package com.vrushabhhirap.hidimbistown

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.vrushabhhirap.hidimbistown.model.TradeOffer

class TownActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val action = intent.action

        // Decoy 3: Lake Reflection Trap
        if (action == "com.vrushabhhirap.hidimbistown.LAKE_REFLECTION") {
            val decoyIntent = Intent().apply {
                putExtra("flag", "SETU{LAKE_REFLECTION_COMPLETE}")
                putExtra("status", "REFLECTED_MAYA")
            }
            setResult(Activity.RESULT_OK, decoyIntent)
            finish()
            return
        }

        // True Stage 8: Market Trade Activity
        if (action == "com.vrushabhhirap.hidimbistown.MARKET_TRADE") {
            val tradeOffer: TradeOffer? = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra("trade_item", TradeOffer::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra("trade_item")
            }

            if (tradeOffer == null) {
                setResult(Activity.RESULT_CANCELED)
                finish()
                return
            }

            val maxCredits = 1000
            val totalCost = tradeOffer.calculateTotal()

            // Integer overflow vulnerability exploitation:
            // High quantity and unit price overflow 32-bit signed integer into negative value
            if (totalCost < 0) {
                val resultIntent = Intent().apply {
                    putExtra("node_id", 13)
                    putExtra("dir", 18) // Next = (13 + 18) % 37 = 31 (Lake)
                    putExtra("k4", "5f4e3d2c1b0a9f8e7d6c5b4a3f2e1d0c")
                    putExtra("n4", "0102030405060708090a0b0c0d0e0f10")
                    putExtra("lake_api_path", "/api/v1/lake/trade")
                    putExtra("status", "TREASURY_UNLOCKED")
                }
                setResult(Activity.RESULT_OK, resultIntent)
            } else if (totalCost in 1..maxCredits) {
                val resultIntent = Intent().apply {
                    putExtra("status", "ORDINARY_TRADE_SUCCESS")
                }
                setResult(Activity.RESULT_OK, resultIntent)
            } else {
                val resultIntent = Intent().apply {
                    putExtra("status", "INSUFFICIENT_CREDITS")
                }
                setResult(Activity.RESULT_CANCELED, resultIntent)
            }
            finish()
        }
    }
}
