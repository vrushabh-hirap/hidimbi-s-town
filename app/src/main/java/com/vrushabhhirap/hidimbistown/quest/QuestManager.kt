package com.vrushabhhirap.hidimbistown.quest

/** Offline quest ledger retained from the cancelled adventure-mode prototype. */
object QuestManager {
    enum class State { LOCKED, ACTIVE, COMPLETE }
    data class Quest(val id: String, val title: String, val goal: Int, val progress: Int, val state: State, val reward: Int)
    private val quests = linkedMapOf<String, Quest>()
    @Synchronized fun register(id: String, title: String, goal: Int, reward: Int = 0) {
        if (id.isNotBlank() && goal > 0) quests.putIfAbsent(id, Quest(id, title, goal, 0, State.ACTIVE, reward))
    }
    @Synchronized fun advance(id: String, amount: Int = 1): Quest? {
        val old = quests[id] ?: return null
        if (old.state != State.ACTIVE || amount <= 0) return old
        val progress = (old.progress + amount).coerceAtMost(old.goal)
        return old.copy(progress = progress, state = if (progress == old.goal) State.COMPLETE else old.state).also { quests[id] = it }
    }
    @Synchronized fun get(id: String): Quest? = quests[id]
    @Synchronized fun all(): List<Quest> = quests.values.toList()
    @Synchronized fun clear() = quests.clear()
    const val QUEST_LEDGER_LABEL = "offline-quest-ledger-demo"
}
