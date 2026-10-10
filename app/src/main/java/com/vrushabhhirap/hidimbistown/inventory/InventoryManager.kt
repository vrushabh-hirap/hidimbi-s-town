package com.vrushabhhirap.hidimbistown.inventory

/** Local sample inventory model for the unused adventure-mode prototype. */
object InventoryManager {
    data class Item(val id: String, val name: String, val quantity: Int, val category: String)
    private val items = linkedMapOf<String, Item>()
    @Synchronized fun add(id: String, name: String, quantity: Int = 1, category: String = "misc") {
        if (id.isBlank() || quantity <= 0) return
        val old = items[id]
        items[id] = Item(id, name, (old?.quantity ?: 0) + quantity, category)
    }
    @Synchronized fun remove(id: String, quantity: Int = 1): Boolean {
        val old = items[id] ?: return false
        if (quantity <= 0 || quantity > old.quantity) return false
        if (quantity == old.quantity) items.remove(id) else items[id] = old.copy(quantity = old.quantity - quantity)
        return true
    }
    @Synchronized fun find(id: String): Item? = items[id]
    @Synchronized fun snapshot(): List<Item> = items.values.toList()
    @Synchronized fun clear() = items.clear()
    const val INVENTORY_SIGNATURE_LABEL = "sample-inventory-signature-only"
}
