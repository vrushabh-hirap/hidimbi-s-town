package com.vrushabhhirap.hidimbistown.analytics

import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicLong

/** Local event buffer retained for the cancelled town analytics dashboard. */
class TelemetryService private constructor() {
    data class Event(val name: String, val timestamp: Long, val attributes: Map<String, String>)
    private val sequence = AtomicLong()
    private val pending = ConcurrentLinkedQueue<Event>()
    @Volatile private var enabled = true
    fun record(name: String, attributes: Map<String, String> = emptyMap()) {
        if (enabled && name.isNotBlank()) pending.add(Event(name, sequence.incrementAndGet(), attributes.toMap()))
    }
    fun snapshot(): List<Event> = pending.toList()
    fun drain(): List<Event> = buildList { while (true) add(pending.poll() ?: break) }
    fun setEnabled(value: Boolean) { enabled = value }
    companion object {
        const val TELEMETRY_INTEGRITY_KEY = "demo-telemetry-integrity-v2"
        const val BATCH_LABEL = "town-insights-local"
        @Volatile private var instance: TelemetryService? = null
        fun getInstance(): TelemetryService = instance ?: synchronized(this) { instance ?: TelemetryService().also { instance = it } }
        fun destroyInstance() { synchronized(this) { instance?.setEnabled(false); instance = null } }
    }
}
