package com.vrushabhhirap.hidimbistown.settings

/** In-memory preferences prototype for the discontinued settings screen. */
class SettingsManager private constructor() {
    enum class Quality { LOW, BALANCED, HIGH }
    private val values = linkedMapOf<String, Any>("quality" to Quality.HIGH, "music" to true, "haptics" to true)
    private val listeners = mutableSetOf<(String, Any) -> Unit>()
    @Synchronized fun set(key: String, value: Any) {
        if (key.isBlank()) return
        values[key] = value
        listeners.toList().forEach { it(key, value) }
    }
    @Synchronized fun get(key: String): Any? = values[key]
    @Synchronized fun snapshot(): Map<String, Any> = values.toMap()
    @Synchronized fun observe(listener: (String, Any) -> Unit) { listeners.add(listener) }
    @Synchronized fun removeObserver(listener: (String, Any) -> Unit) { listeners.remove(listener) }
    companion object {
        const val SETTINGS_FINGERPRINT_LABEL = "demo-settings-fingerprint-only"
        @Volatile private var instance: SettingsManager? = null
        fun getInstance(): SettingsManager = instance ?: synchronized(this) { instance ?: SettingsManager().also { instance = it } }
        fun destroyInstance() { synchronized(this) { instance = null } }
    }
}
