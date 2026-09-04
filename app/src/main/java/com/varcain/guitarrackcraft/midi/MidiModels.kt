/*
 * Copyright (C) 2026 Kamil Lulko <kamil.lulko@gmail.com>
 *
 * This file is part of Guitar RackCraft.
 *
 * Guitar RackCraft is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Guitar RackCraft is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Guitar RackCraft. If not, see <https://www.gnu.org/licenses/>.
 */

package com.varcain.guitarrackcraft.midi

import org.json.JSONObject
import java.util.UUID

enum class MidiMessageType {
    CC,
    PC,
    NOTE
}

enum class MidiActionType(val displayName: String) {
    SLOT_BYPASS_TOGGLE("Toggle Slot Bypass"),
    SLOT_BYPASS_ON("Slot Bypass ON"),
    SLOT_BYPASS_OFF("Slot Bypass OFF"),
    MASTER_CHAIN_BYPASS("Toggle Master Bypass"),
    PARAMETER_CONTROL("Control Parameter"),
    PRESET_SWITCH("Load Preset"),
    PRESET_NEXT("Next Preset"),
    PRESET_PREV("Previous Preset")
}

data class MidiMapping(
    val id: String = UUID.randomUUID().toString(),
    val name: String,
    val channel: Int = -1, // -1 = Omni (any channel)
    val messageType: MidiMessageType = MidiMessageType.CC,
    val controlNumber: Int, // CC# or PC# or Note#
    val actionType: MidiActionType,
    val targetSlotIndex: Int = 0,
    val targetPortIndex: Int = 0,
    val minVal: Float = 0f,
    val maxVal: Float = 1f,
    val isToggle: Boolean = true
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("id", id)
            put("name", name)
            put("channel", channel)
            put("messageType", messageType.name)
            put("controlNumber", controlNumber)
            put("actionType", actionType.name)
            put("targetSlotIndex", targetSlotIndex)
            put("targetPortIndex", targetPortIndex)
            put("minVal", minVal.toDouble())
            put("maxVal", maxVal.toDouble())
            put("isToggle", isToggle)
        }
    }

    companion object {
        fun fromJson(obj: JSONObject): MidiMapping {
            return MidiMapping(
                id = obj.optString("id", UUID.randomUUID().toString()),
                name = obj.optString("name", "Mapping"),
                channel = obj.optInt("channel", -1),
                messageType = try {
                    MidiMessageType.valueOf(obj.optString("messageType", "CC"))
                } catch (_: Exception) { MidiMessageType.CC },
                controlNumber = obj.optInt("controlNumber", 0),
                actionType = try {
                    MidiActionType.valueOf(obj.optString("actionType", "SLOT_BYPASS_TOGGLE"))
                } catch (_: Exception) { MidiActionType.SLOT_BYPASS_TOGGLE },
                targetSlotIndex = obj.optInt("targetSlotIndex", 0),
                targetPortIndex = obj.optInt("targetPortIndex", 0),
                minVal = obj.optDouble("minVal", 0.0).toFloat(),
                maxVal = obj.optDouble("maxVal", 1.0).toFloat(),
                isToggle = obj.optBoolean("isToggle", true)
            )
        }
    }
}

data class MidiEvent(
    val channel: Int,
    val messageType: MidiMessageType,
    val number: Int,
    val value: Int, // 0..127
    val timestamp: Long = System.currentTimeMillis()
)
