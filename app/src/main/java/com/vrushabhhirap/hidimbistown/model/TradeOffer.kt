package com.vrushabhhirap.hidimbistown.model

import android.os.Parcel
import android.os.Parcelable

class TradeOffer(
    val itemName: String,
    val quantity: Int,
    val unitPrice: Int
) : Parcelable {

    constructor(parcel: Parcel) : this(
        parcel.readString() ?: "",
        parcel.readInt(),
        parcel.readInt()
    )

    fun calculateTotal(): Int {
        // Intended arithmetic flaw: 32-bit signed integer overflow
        return quantity * unitPrice
    }

    override fun writeToParcel(parcel: Parcel, flags: Int) {
        parcel.writeString(itemName)
        parcel.writeInt(quantity)
        parcel.writeInt(unitPrice)
    }

    override fun describeContents(): Int = 0

    companion object CREATOR : Parcelable.Creator<TradeOffer> {
        override fun createFromParcel(parcel: Parcel): TradeOffer {
            return TradeOffer(parcel)
        }

        override fun newArray(size: Int): Array<TradeOffer?> {
            return arrayOfNulls(size)
        }
    }
}
