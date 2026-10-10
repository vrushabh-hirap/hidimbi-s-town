package com.vrushabhhirap.hidimbistown

import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.card.MaterialCardView
import com.vrushabhhirap.hidimbistown.model.TradeOffer
import com.vrushabhhirap.hidimbistown.native.NativeBridge

/**
 * The player-facing town client. Navigation and sample trade cards are local
 * presentation only; the exported component chain remains the challenge API.
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val INK = 0xFF203B35.toInt()
        private const val FOREST = 0xFF27654E.toInt()
        private const val MOSS = 0xFF789276.toInt()
        private const val PAPER = 0xFFF5F1E7.toInt()
        private const val CARD = 0xFFFFFEFA.toInt()
        private const val GOLD = 0xFFC49343.toInt()
        private const val MUTED = 0xFF6D7770.toInt()

        val LANDMARKS = listOf(
            Landmark("Forest", 17, "🌲", "A quiet trail beneath old cedar trees"),
            Landmark("Temple", 4, "🛕", "Incense curls above the stone steps"),
            Landmark("Cave", 29, "🪨", "Cool air drifts from the hillside"),
            Landmark("Market", 13, "🧺", "Spices, stories, and a busy square"),
            Landmark("Palace", 8, "🏯", "Lanterns glow behind the outer wall"),
            Landmark("Lake", 31, "🌊", "A silver path crosses the water"),
            Landmark("Mosaic of the Nine Wells", 6, "🧿", "Nine tiled wells below the town"),
            Landmark("The Scriptorium", 33, "📜", "A room full of ink and old alphabets"),
            Landmark("The Drum Tower", 22, "🥁", "A rhythm carries over the rooftops"),
            Landmark("The Courier's Ledger", 14, "✉️", "A ledger tied shut with red cord"),
            Landmark("The Granary", 25, "🌾", "Harvest stores beneath a timber roof"),
            Landmark("The Observatory", 19, "🔭", "A brass telescope points north"),
            Landmark("The Keeper's Whistle", 11, "🪈", "A small whistle hangs by the gate"),
            Landmark("The Vault", 1, "🗝️", "A sealed door under the riverbank")
        )

        private val SAMPLE_OFFERS = listOf(
            TradeOffer("Cedar tea", 2, 8),
            TradeOffer("Market oranges", 4, 3),
            TradeOffer("Blue river glass", 1, 21),
            TradeOffer("Woven travel pouch", 1, 16)
        )
    }

    data class Landmark(val name: String, val node: Int, val icon: String, val detail: String)
    private enum class Screen { HOME, MARKET, INVENTORY, MAP }

    private var currentScreen = Screen.HOME
    private val landmarkState = mutableMapOf<String, String>()
    private lateinit var page: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = INK
        window.navigationBarColor = INK
        window.decorView.systemUiVisibility = 0

        // Keep the native initialization used by the original challenge app.
        NativeBridge.initNative()
        currentScreen = savedInstanceState?.getString("town_screen")
            ?.let { runCatching { Screen.valueOf(it) }.getOrNull() } ?: Screen.HOME
    }

    override fun onResume() {
        super.onResume()
        render()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString("town_screen", currentScreen.name)
        super.onSaveInstanceState(outState)
    }

    private fun render() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(PAPER)
        }
        root.addView(topBar(), LinearLayout.LayoutParams(-1, dp(72)))
        val scroll = ScrollView(this).apply {
            clipToPadding = false
            setPadding(dp(18), dp(16), dp(18), dp(16))
            isFillViewport = true
        }
        page = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        scroll.addView(page)
        root.addView(scroll, LinearLayout.LayoutParams(0, 0, 1f))
        root.addView(bottomNavigation(), LinearLayout.LayoutParams(-1, dp(72)))
        setContentView(root)

        when (currentScreen) {
            Screen.HOME -> showHome()
            Screen.MARKET -> showMarket()
            Screen.INVENTORY -> showInventory()
            Screen.MAP -> showMap()
        }
    }

    private fun topBar(): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(dp(20), 0, dp(20), 0)
        setBackgroundColor(INK)
        val title = column {
            text("HIDIMBI'S TOWN", 12f, 0xFFD6C694.toInt(), bold = true)
            text("A riverside trading village", 18f, Color.WHITE, bold = true)
        }
        addView(title, LinearLayout.LayoutParams(0, -2, 1f))
        text("☀️", 25f, Color.WHITE).also { addView(it) }
    }

    private fun bottomNavigation(): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER
        setPadding(dp(8), dp(8), dp(8), dp(8))
        setBackgroundColor(CARD)
        listOf(
            Triple(Screen.HOME, "⌂", "Home"),
            Triple(Screen.MARKET, "🧺", "Market"),
            Triple(Screen.INVENTORY, "🎒", "Bag"),
            Triple(Screen.MAP, "🗺️", "Map")
        ).forEach { (screen, icon, label) ->
            val button = Button(this@MainActivity).apply {
                text = "$icon  $label"
                textSize = 12f
                isAllCaps = false
                setTextColor(if (currentScreen == screen) FOREST else MUTED)
                backgroundTintList = ColorStateList.valueOf(CARD)
                setPadding(dp(3), 0, dp(3), 0)
                setOnClickListener {
                    currentScreen = screen
                    render()
                }
            }
            addView(button, LinearLayout.LayoutParams(0, -1, 1f))
        }
    }

    private fun showHome() {
        page.addView(eyebrow("MONDAY • EARLY MORNING"))
        page.addView(heading("Good morning, traveler"))
        page.addView(body("The gates are open. Take a slow walk through town."))
        page.addView(space(16))
        page.addView(card {
            text("🌤️   CLEAR SKIES BY THE RIVER", 12f, GOLD, bold = true)
            text("A bright day for the market", 19f, INK, bold = true, top = 7)
            text("Light wind · 22° · Lantern festival in 3 days", 13f, MUTED, top = 5)
        })
        page.addView(space(12))
        page.addView(card {
            text("A NOTE ABOUT THE MAP", 11f, FOREST, bold = true)
            text("The map lies.", 23f, INK, bold = true, top = 7)
            text("It is a lovely guide for a stroll, but the old town keeps a few paths to itself.",
                14f, MUTED, top = 5)
        })
        page.addView(sectionTitle("Around town", "14 landmarks"))
        LANDMARKS.forEach { landmark ->
            val status = landmarkState.getOrPut(landmark.name) { probe(landmark.name, landmark.node) }
            page.addView(landmarkCard(landmark, status))
        }
    }

    private fun showMarket() {
        page.addView(eyebrow("THE MARKET SQUARE"))
        page.addView(heading("A little something for the road"))
        page.addView(body("Locally made goods from the people of Hidimbi's Town."))
        page.addView(space(12))
        page.addView(card {
            text("🪙   YOUR PURSE", 11f, GOLD, bold = true)
            text("240 town coins", 21f, INK, bold = true, top = 6)
            text("Plenty for tea, a snack, and a keepsake.", 13f, MUTED, top = 4)
        })
        page.addView(sectionTitle("Today's stalls", "4 open"))
        SAMPLE_OFFERS.forEach { offer ->
            page.addView(card {
                val (icon, note) = when (offer.itemName) {
                    "Cedar tea" -> "🍵" to "Freshly dried on the north ridge"
                    "Market oranges" -> "🍊" to "Sweet fruit from the southern grove"
                    "Blue river glass" -> "🔮" to "Polished by the bend in the river"
                    else -> "🧵" to "Hand woven in the old quarter"
                }
                row {
                    text(icon, 29f, INK)
                    addView(spaceWidth(12))
                    column {
                        text(offer.itemName, 16f, INK, bold = true)
                        text(note, 12f, MUTED, top = 3)
                        text("${offer.quantity} in a bundle · ${offer.unitPrice} coins each",
                            12f, FOREST, top = 5)
                    }
                }
                val buy = Button(this@MainActivity).apply {
                    text = "Add to basket"
                    isAllCaps = false
                    setTextColor(Color.WHITE)
                    backgroundTintList = ColorStateList.valueOf(FOREST)
                    setOnClickListener {
                        Toast.makeText(this@MainActivity,
                            "${offer.itemName} added to your basket", Toast.LENGTH_SHORT).show()
                    }
                }
                addView(buy, LinearLayout.LayoutParams(-1, dp(44)).apply { topMargin = dp(12) })
            })
        }
        page.addView(space(12))
        page.addView(body("Market orders here are a town-life preview. The Market landmark remains one of the challenge's deeper paths."))
    }

    private fun showInventory() {
        page.addView(eyebrow("TRAVELER PROFILE"))
        page.addView(heading("Your satchel"))
        page.addView(body("A few useful things for wandering around the river towns."))
        page.addView(space(12))
        page.addView(card {
            row {
                text("🧑🏽‍🌾", 38f, INK)
                addView(spaceWidth(12))
                column {
                    text("Town visitor", 18f, INK, bold = true)
                    text("Joined this morning · Level 1 wanderer", 12f, MUTED, top = 3)
                }
            }
        })
        page.addView(sectionTitle("In your bag", "3 items"))
        listOf(
            "🧭" to ("Pocket compass" to "Points north, most of the time"),
            "🍵" to ("Cedar tea" to "A warm cup for the road"),
            "🧵" to ("Woven pouch" to "Small, sturdy, and locally made")
        ).forEach { (icon, item) ->
            page.addView(card {
                row {
                    text(icon, 28f, INK)
                    addView(spaceWidth(12))
                    column {
                        text(item.first, 16f, INK, bold = true)
                        text(item.second, 13f, MUTED, top = 3)
                    }
                }
            })
        }
        page.addView(sectionTitle("Town journal", "Today"))
        page.addView(card {
            text("A new day in Hidimbi's Town", 15f, INK, bold = true)
            text("You arrived by the eastern bridge. Someone at the market said the lake is unusually quiet.",
                13f, MUTED, top = 6)
        })
    }

    private fun showMap() {
        page.addView(eyebrow("HIDIMBI'S TOWN · WALKING MAP"))
        page.addView(heading("Find your way"))
        page.addView(body("A hand-drawn guide for the visible streets and familiar landmarks."))
        page.addView(space(12))
        page.addView(card {
            text("⚠️   A LOCAL'S REMINDER", 11f, GOLD, bold = true)
            text("The map lies.", 23f, INK, bold = true, top = 7)
            text("Some routes on this paper are out of order. Follow what the town tells you, not only what the drawing shows.",
                14f, MUTED, top = 5)
        })
        page.addView(space(12))
        page.addView(mapIllustration())
        page.addView(sectionTitle("Landmark guide", "Visible order"))
        LANDMARKS.forEachIndexed { index, place ->
            page.addView(card {
                row {
                    text(place.icon, 25f, INK)
                    addView(spaceWidth(10))
                    column {
                        text("${index + 1}.  ${place.name}", 15f, INK, bold = true)
                        text(place.detail, 12f, MUTED, top = 3)
                    }
                }
            })
        }
    }

    private fun mapIllustration(): View = card {
        val drawing = LinearLayout(this@MainActivity).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            background = rounded(0xFFE4EAD8.toInt(), 20)
            setPadding(dp(14), dp(18), dp(14), dp(18))
        }
        drawing.addView(text("🌲       🛕       🏯", 27f, INK).apply { gravity = Gravity.CENTER })
        drawing.addView(text("   ╲       │       ╱   ", 18f, MOSS).apply { gravity = Gravity.CENTER })
        drawing.addView(text("🪨  ──  🏘️  ──  🧺", 26f, INK).apply { gravity = Gravity.CENTER })
        drawing.addView(text("   ╱       │       ╲   ", 18f, MOSS).apply { gravity = Gravity.CENTER })
        drawing.addView(text("🌾       🔭       🌊", 27f, INK).apply { gravity = Gravity.CENTER })
        addView(drawing, LinearLayout.LayoutParams(-1, dp(190)))
        text("A sketch from the town notice board", 12f, MUTED, top = 9)
    }

    private fun landmarkCard(place: Landmark, status: String): View = card {
        row {
            text(place.icon, 28f, INK)
            addView(spaceWidth(12))
            column {
                text(place.name, 15f, INK, bold = true)
                text(place.detail, 12f, MUTED, top = 2)
            }
            val badge = TextView(this@MainActivity).apply {
                text = status
                textSize = 10f
                setTextColor(FOREST)
                setPadding(dp(7), dp(5), dp(7), dp(5))
                background = rounded(0xFFE8F0E7.toInt(), 16)
            }
            addView(badge)
        }
    }

    /**
     * The old market calculation lives in the exported trade parcel. UI parity
     * notes for the separate inventory prototype are under inventory/; these
     * read-only cards do not create or submit those parcels.
     */
    private fun probe(name: String, node: Int): String {
        return try {
            when (node) {
                17, 29 -> {
                    val cursor = contentResolver.query(
                        android.net.Uri.parse("content://com.vrushabhhirap.hidimbistown.provider/cave"),
                        null, null, null, null)
                    val seen = cursor?.let { if (it.moveToFirst()) it.getString(0) else "" }
                    cursor?.close()
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

    private fun sectionTitle(title: String, trailing: String): View = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(0, dp(19), 0, dp(9))
        addView(text(title, 18f, INK, bold = true), LinearLayout.LayoutParams(0, -2, 1f))
        addView(text(trailing, 11f, MUTED))
    }

    private fun heading(value: String): View = text(value, 26f, INK, bold = true).apply {
        setPadding(0, dp(4), 0, dp(2))
    }

    private fun eyebrow(value: String): View = text(value, 10f, FOREST, bold = true).apply {
        letterSpacing = 0.12f
    }

    private fun body(value: String): View = text(value, 14f, MUTED).apply {
        setLineSpacing(dp(2).toFloat(), 1f)
        setPadding(0, dp(3), 0, 0)
    }

    private fun text(value: String, size: Float, color: Int, bold: Boolean = false, top: Int = 0) =
        TextView(this).apply {
            text = value
            textSize = size
            setTextColor(color)
            if (bold) setTypeface(typeface, Typeface.BOLD)
            if (top > 0) setPadding(0, dp(top), 0, 0)
        }

    private fun column(block: LinearLayout.() -> Unit): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        block()
    }

    private fun row(block: LinearLayout.() -> Unit): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        block()
    }

    private fun card(block: LinearLayout.() -> Unit): View = MaterialCardView(this).apply {
        radius = dp(18).toFloat()
        cardElevation = dp(1).toFloat()
        setCardBackgroundColor(CARD)
        strokeWidth = 0
        val content = LinearLayout(this@MainActivity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(15), dp(14), dp(15), dp(14))
            block()
        }
        addView(content)
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(10) }
    }

    private fun space(height: Int): View = View(this).apply {
        layoutParams = LinearLayout.LayoutParams(1, dp(height))
    }

    private fun spaceWidth(width: Int): View = View(this).apply {
        layoutParams = LinearLayout.LayoutParams(dp(width), 1)
    }

    private fun rounded(color: Int, radiusDp: Int) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radiusDp).toFloat()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
}
