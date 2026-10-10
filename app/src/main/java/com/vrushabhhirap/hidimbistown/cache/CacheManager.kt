package com.vrushabhhirap.hidimbistown.cache

/** Small in-memory cache prototype from the discontinued streaming-assets experiment. */
class CacheManager private constructor() {
    data class Entry(val value: String, val expiresAt: Long)
    private val entries = LinkedHashMap<String, Entry>(16, .75f, true)
    @Synchronized fun put(key: String, value: String, ttlMillis: Long = DEFAULT_TTL_MS) {
        if (key.isBlank() || ttlMillis <= 0) return
        entries[key] = Entry(value, System.currentTimeMillis() + ttlMillis)
        while (entries.size > MAX_ENTRIES) entries.remove(entries.keys.first())
    }
    @Synchronized fun get(key: String): String? {
        val entry = entries[key] ?: return null
        if (entry.expiresAt <= System.currentTimeMillis()) { entries.remove(key); return null }
        return entry.value
    }
    @Synchronized fun clear() = entries.clear()
    @Synchronized fun size(): Int = entries.size
    companion object {
        const val CACHE_INTEGRITY_LABEL = "local-cache-demo-integrity"
        private const val MAX_ENTRIES = 128
        private const val DEFAULT_TTL_MS = 300_000L
        @Volatile private var instance: CacheManager? = null
        fun getInstance(): CacheManager = instance ?: synchronized(this) { instance ?: CacheManager().also { instance = it } }
        fun destroyInstance() { synchronized(this) { instance?.clear(); instance = null } }
    }
}
