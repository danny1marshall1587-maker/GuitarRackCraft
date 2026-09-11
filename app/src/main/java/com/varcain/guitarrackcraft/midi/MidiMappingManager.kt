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

import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Manages MIDI mappings, persistence, presets, and MIDI Learn mode.
 */
object MidiMappingManager {

    private const val TAG = "MidiMappingManager"
    private const val FILE_NAME = "midi_mappings.json"

    private val _mappings = MutableStateFlow<List<MidiMapping>>(emptyList())
    val mappings: StateFlow<List<MidiMapping>> = _mappings.asStateFlow()

    private val _isLearning = MutableStateFlow(false)
    val isLearning: StateFlow<Boolean> = _isLearning.asStateFlow()

    private var learnTarget: MidiMapping? = null

    fun initialize(context: Context) {
        val file = File(context.filesDir, FILE_NAME)
        if (file.exists()) {
            loadFromFile(file)
        } else {
            loadDefaultPacerMappings(context)
        }
    }

    private fun loadFromFile(file: File) {
        try {
            val text = file.readText()
            val array = JSONArray(text)
            val list = mutableListOf<MidiMapping>()
            for (i in 0 until array.length()) {
                list.add(MidiMapping.fromJson(array.getJSONObject(i)))
            }
            _mappings.value = list
            Log.i(TAG, "Loaded ${list.size} MIDI mappings from file")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to read MIDI mappings: ${e.message}", e)
        }
    }

    fun saveToFile(context: Context) {
        try {
            val file = File(context.filesDir, FILE_NAME)
            val array = JSONArray()
            _mappings.value.forEach { array.put(it.toJson()) }
            file.writeText(array.toString(2))
            Log.i(TAG, "Saved ${_mappings.value.size} MIDI mappings")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to save MIDI mappings: ${e.message}", e)
        }
    }

    fun loadDefaultPacerMappings(context: Context) {
        val defaults = listOf(
            MidiMapping(
                name = "Pacer FS 1 -> Slot 1 Bypass",
                messageType = MidiMessageType.CC,
                controlNumber = 21,
                actionType = MidiActionType.SLOT_BYPASS_TOGGLE,
                targetSlotIndex = 0
            ),
            MidiMapping(
                name = "Pacer FS 2 -> Slot 2 Bypass",
                messageType = MidiMessageType.CC,
                controlNumber = 22,
                actionType = MidiActionType.SLOT_BYPASS_TOGGLE,
                targetSlotIndex = 1
            ),
            MidiMapping(
                name = "Pacer FS 3 -> Slot 3 Bypass",
                messageType = MidiMessageType.CC,
                controlNumber = 23,
                actionType = MidiActionType.SLOT_BYPASS_TOGGLE,
                targetSlotIndex = 2
            ),
            MidiMapping(
                name = "Pacer FS 4 -> Slot 4 Bypass",
                messageType = MidiMessageType.CC,
                controlNumber = 24,
                actionType = MidiActionType.SLOT_BYPASS_TOGGLE,
                targetSlotIndex = 3
            ),
            MidiMapping(
                name = "Pacer FS 5 -> Slot 5 Bypass",
                messageType = MidiMessageType.CC,
                controlNumber = 25,
                actionType = MidiActionType.SLOT_BYPASS_TOGGLE,
                targetSlotIndex = 4
            ),
            MidiMapping(
                name = "Pacer FS 6 -> Master Bypass",
                messageType = MidiMessageType.CC,
                controlNumber = 26,
                actionType = MidiActionType.MASTER_CHAIN_BYPASS
            ),
            MidiMapping(
                name = "Pacer EXP 1 -> Slot 1 Wah/Gain",
                messageType = MidiMessageType.CC,
                controlNumber = 11, // Standard Expression CC
                actionType = MidiActionType.PARAMETER_CONTROL,
                targetSlotIndex = 0,
                targetPortIndex = 0,
                minVal = 0f,
                maxVal = 1f
            ),
            MidiMapping(
                name = "Pacer EXP 2 -> Slot 2 Param/Vol",
                messageType = MidiMessageType.CC,
                controlNumber = 7, // Standard Volume CC
                actionType = MidiActionType.PARAMETER_CONTROL,
                targetSlotIndex = 1,
                targetPortIndex = 0,
                minVal = 0f,
                maxVal = 1f
            ),
            MidiMapping(
                name = "Pacer Presets (Program Change)",
                messageType = MidiMessageType.PC,
                controlNumber = 0,
                actionType = MidiActionType.PRESET_SWITCH
            )
        )
        _mappings.value = defaults
        saveToFile(context)
        Log.i(TAG, "Default Nektar Pacer mappings loaded")
    }

    fun addOrUpdateMapping(context: Context, mapping: MidiMapping) {
        val current = _mappings.value.toMutableList()
        val idx = current.indexOfFirst { it.id == mapping.id }
        if (idx >= 0) {
            current[idx] = mapping
        } else {
            current.add(mapping)
        }
        _mappings.value = current
        saveToFile(context)
    }

    fun removeMapping(context: Context, id: String) {
        val current = _mappings.value.toMutableList()
        current.removeAll { it.id == id }
        _mappings.value = current
        saveToFile(context)
    }

    fun getMappingForSlotAndPort(slotIndex: Int, portIndex: Int): MidiMapping? {
        return _mappings.value.firstOrNull {
            it.actionType == MidiActionType.PARAMETER_CONTROL &&
            it.targetSlotIndex == slotIndex &&
            it.targetPortIndex == portIndex
        }
    }

    fun getAllMappingsForSlotAndPort(slotIndex: Int, portIndex: Int): List<MidiMapping> {
        return _mappings.value.filter {
            it.actionType == MidiActionType.PARAMETER_CONTROL &&
            it.targetSlotIndex == slotIndex &&
            it.targetPortIndex == portIndex
        }
    }


    /**
     * Start MIDI Learn mode targeting a specific action/slot/port.
     */
    fun startLearn(target: MidiMapping) {
        learnTarget = target
        _isLearning.value = true
        Log.i(TAG, "MIDI Learn started for: ${target.name}")
    }

    fun cancelLearn() {
        learnTarget = null
        _isLearning.value = false
        Log.i(TAG, "MIDI Learn cancelled")
    }

    /**
     * Handle incoming MIDI event when Learn mode is active.
     * @return true if learn consumed and handled the event.
     */
    fun onMidiEventDuringLearn(context: Context, event: MidiEvent): Boolean {
        if (!_isLearning.value || learnTarget == null) return false

        val target = learnTarget ?: return false
        val newMapping = target.copy(
            channel = event.channel,
            messageType = event.messageType,
            controlNumber = event.number
        )

        addOrUpdateMapping(context, newMapping)
        learnTarget = null
        _isLearning.value = false
        Log.i(TAG, "MIDI Learn completed! Mapped ${event.messageType} #${event.number} on Ch ${event.channel} to ${newMapping.name}")
        return true
    }
}
