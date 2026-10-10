package com.vrushabhhirap.hidimbistown.notification

/** In-app reminder queue prototype; it does not post Android system notifications. */
object NotificationManager {
    data class Reminder(val id: Long, val title: String, val body: String, val dueAt: Long)
    private val reminders = linkedMapOf<Long, Reminder>()
    private var nextId = 1L
    @Synchronized fun schedule(title: String, body: String, delayMillis: Long): Reminder {
        val item = Reminder(nextId++, title, body, System.currentTimeMillis() + delayMillis.coerceAtLeast(0))
        reminders[item.id] = item
        return item
    }
    @Synchronized fun due(now: Long = System.currentTimeMillis()): List<Reminder> = reminders.values.filter { it.dueAt <= now }
    @Synchronized fun dismiss(id: Long): Boolean = reminders.remove(id) != null
    @Synchronized fun snapshot(): List<Reminder> = reminders.values.toList()
    @Synchronized fun clear() = reminders.clear()
    const val CHANNEL_LABEL = "town-reminder-prototype"
}
