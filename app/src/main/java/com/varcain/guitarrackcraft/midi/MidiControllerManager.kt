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
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiInputPort
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.varcain.guitarrackcraft.engine.NativeEngine
import com.varcain.guitarrackcraft.engine.PresetManager
import com.varcain.guitarrackcraft.engine.RackManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Manages USB MIDI device connections, parses incoming MIDI byte streams,
 * dispatches mapped actions to the audio engine, and sends feedback to controllers like the Nektar Pacer.
 */
object MidiControllerManager {

    private const val TAG = "MidiControllerManager"

    private var midiManager: MidiManager? = null
    private val scope = CoroutineScope(Dispatchers.Main)

    private val _connectedDeviceName = MutableStateFlow<String?>(null)
    val connectedDeviceName: StateFlow<String?> = _connectedDeviceName.asStateFlow()

    private val _lastMidiEvent = MutableStateFlow<MidiEvent?>(null)
    val lastMidiEvent: StateFlow<MidiEvent?> = _lastMidiEvent.asStateFlow()

    private val _midiActivityTimestamp = MutableStateFlow(0L)
    val midiActivityTimestamp: StateFlow<Long> = _midiActivityTimestamp.asStateFlow()

    private var activeMidiDevice: MidiDevice? = null
    private var activeOutputPort: MidiOutputPort? = null
    private var activeFeedbackPort: MidiInputPort? = null

    private var masterBypassed = false
    private var appContext: Context? = null
    private val presetManager = PresetManager(NativeEngine.getInstance())
    private var currentPresetIndex = 0

    private val deviceCallback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(device: MidiDeviceInfo) {
            Log.i(TAG, "MIDI device attached: ${getDeviceName(device)}")
            connectDevice(device)
        }

        override fun onDeviceRemoved(device: MidiDeviceInfo) {
            Log.i(TAG, "MIDI device detached: ${getDeviceName(device)}")
            disconnectDevice()
        }
    }

    fun initialize(context: Context) {
        val appContext = context.applicationContext
        MidiMappingManager.initialize(appContext)

        midiManager = appContext.getSystemService(Context.MIDI_SERVICE) as? MidiManager
        if (midiManager == null) {
            Log.w(TAG, "Android MidiManager not available on this device")
            return
        }

        try {
            val handler = Handler(Looper.getMainLooper())
            midiManager?.registerDeviceCallback(deviceCallback, handler)

            // Scan for already-attached devices
            val devices = midiManager?.devices ?: emptyArray()
            Log.i(TAG, "Found ${devices.size} existing MIDI devices")
            for (dev in devices) {
                if (dev.outputPortCount > 0 || dev.inputPortCount > 0) {
                    connectDevice(dev)
                    break
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to initialize MIDI listener: ${e.message}", e)
        }
    }

    private fun connectDevice(info: MidiDeviceInfo) {
        val mm = midiManager ?: return
        val name = getDeviceName(info)
        Log.i(TAG, "Attempting to connect to MIDI device: $name")

        mm.openDevice(info, { device ->
            if (device != null) {
                activeMidiDevice = device
                _connectedDeviceName.value = name
                Log.i(TAG, "Successfully opened MIDI device: $name")

                // Open controller's output port (which sends data to our receiver)
                if (info.outputPortCount > 0) {
                    val port = device.openOutputPort(0)
                    if (port != null) {
                        activeOutputPort = port
                        port.connect(object : MidiReceiver() {
                            override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
                                processMidiBytes(msg, offset, count)
                            }
                        })
                        Log.i(TAG, "Connected to MIDI output port 0")
                    }
                }

                // Open controller's input port (to send LED/state feedback if supported)
                if (info.inputPortCount > 0) {
                    try {
                        activeFeedbackPort = device.openInputPort(0)
                        Log.i(TAG, "Opened MIDI input port 0 for controller feedback")
                    } catch (e: Exception) {
                        Log.w(TAG, "Could not open feedback port: ${e.message}")
                    }
                }
            } else {
                Log.e(TAG, "Failed to open MIDI device: $name")
            }
        }, Handler(Looper.getMainLooper()))
    }

    fun disconnectDevice() {
        try {
            activeOutputPort?.close()
            activeFeedbackPort?.close()
            activeMidiDevice?.close()
        } catch (_: Exception) {}
        activeOutputPort = null
        activeFeedbackPort = null
        activeMidiDevice = null
        _connectedDeviceName.value = null
        Log.i(TAG, "MIDI device disconnected")
    }

    private fun getDeviceName(info: MidiDeviceInfo): String {
        val props = info.properties
        val product = props.getString(MidiDeviceInfo.PROPERTY_PRODUCT)
        val name = props.getString(MidiDeviceInfo.PROPERTY_NAME)
        val manufacturer = props.getString(MidiDeviceInfo.PROPERTY_MANUFACTURER)
        return listOfNotNull(manufacturer, product ?: name).filter { it.isNotBlank() }
            .joinToString(" ").ifEmpty { "USB MIDI Device" }
    }

    /**
     * Parse incoming raw MIDI bytes into structured messages.
     */
    private fun processMidiBytes(bytes: ByteArray, offset: Int, count: Int) {
        var i = offset
        val end = offset + count

        while (i < end) {
            val b = bytes[i].toInt() and 0xFF

            // Filter real-time messages (clock, active sensing, etc.)
            if (b >= 0xF8) {
                i++
                continue
            }

            val status = b and 0xF0
            val channel = (b and 0x0F) + 1

            when (status) {
                0x80 -> { // Note Off
                    if (i + 2 < end) {
                        val note = bytes[i + 1].toInt() and 0x7F
                        val vel = bytes[i + 2].toInt() and 0x7F
                        dispatchMidiEvent(MidiEvent(channel, MidiMessageType.NOTE, note, 0))
                        i += 3
                    } else break
                }
                0x90 -> { // Note On
                    if (i + 2 < end) {
                        val note = bytes[i + 1].toInt() and 0x7F
                        val vel = bytes[i + 2].toInt() and 0x7F
                        val type = if (vel == 0) MidiMessageType.NOTE else MidiMessageType.NOTE
                        dispatchMidiEvent(MidiEvent(channel, type, note, vel))
                        i += 3
                    } else break
                }
                0xB0 -> { // Control Change (CC)
                    if (i + 2 < end) {
                        val ccNumber = bytes[i + 1].toInt() and 0x7F
                        val ccValue = bytes[i + 2].toInt() and 0x7F
                        dispatchMidiEvent(MidiEvent(channel, MidiMessageType.CC, ccNumber, ccValue))
                        i += 3
                    } else break
                }
                0xC0 -> { // Program Change (PC)
                    if (i + 1 < end) {
                        val progNumber = bytes[i + 1].toInt() and 0x7F
                        dispatchMidiEvent(MidiEvent(channel, MidiMessageType.PC, progNumber, progNumber))
                        i += 2
                    } else break
                }
                else -> {
                    // Skip unhandled status or data bytes
                    i++
                }
            }
        }
    }

    private fun dispatchMidiEvent(event: MidiEvent) {
        scope.launch {
            _lastMidiEvent.value = event
            _midiActivityTimestamp.value = System.currentTimeMillis()

            // Check if MIDI Learn is active
            if (MidiMappingManager.isLearning.value) {
                // Ignore zero velocity Note Off during learn
                if (event.messageType == MidiMessageType.NOTE && event.value == 0) return@launch
                val ctx = appContext
                if (ctx != null) {
                    if (MidiMappingManager.onMidiEventDuringLearn(ctx, event)) {
                        return@launch
                    }
                }
            }

            // Normal dispatch against mappings
            executeMappings(event)
        }
    }

    private fun executeMappings(event: MidiEvent) {
        val mappings = MidiMappingManager.mappings.value
        for (mapping in mappings) {
            // Check channel (-1 = Omni)
            if (mapping.channel != -1 && mapping.channel != event.channel) continue

            // Check message type and control number
            if (mapping.messageType != event.messageType) continue

            // For Program Change, controlNumber is ignored or matches bank
            if (mapping.messageType != MidiMessageType.PC && mapping.controlNumber != event.number) continue

            applyAction(mapping, event)
        }
    }

    private fun applyAction(mapping: MidiMapping, event: MidiEvent) {
        when (mapping.actionType) {
            MidiActionType.SLOT_BYPASS_TOGGLE -> {
                // For footswitches: only toggle on press (value > 0 or 127)
                if (event.value > 0) {
                    val next = RackManager.togglePluginBypass(mapping.targetSlotIndex)
                    Log.i(TAG, "Slot ${mapping.targetSlotIndex} bypass toggled -> $next via CC #${event.number}")
                    sendFeedbackCc(event.number, if (!next) 127 else 0)
                }
            }
            MidiActionType.SLOT_BYPASS_ON -> {
                RackManager.setPluginBypass(mapping.targetSlotIndex, true)
                sendFeedbackCc(event.number, 0)
            }
            MidiActionType.SLOT_BYPASS_OFF -> {
                RackManager.setPluginBypass(mapping.targetSlotIndex, false)
                sendFeedbackCc(event.number, 127)
            }
            MidiActionType.MASTER_CHAIN_BYPASS -> {
                if (event.value > 0) {
                    masterBypassed = !masterBypassed
                    NativeEngine.getInstance().setChainBypass(masterBypassed)
                    Log.i(TAG, "Master chain bypass toggled -> $masterBypassed")
                    sendFeedbackCc(event.number, if (!masterBypassed) 127 else 0)
                }
            }
            MidiActionType.PARAMETER_CONTROL -> {
                // Scale 0..127 to [minVal, maxVal]
                val normalized = event.value / 127.0f
                val scaled = mapping.minVal + normalized * (mapping.maxVal - mapping.minVal)
                RackManager.setParameter(mapping.targetSlotIndex, mapping.targetPortIndex, scaled)
            }
            MidiActionType.PRESET_SWITCH -> {
                val ctx = appContext ?: return
                val presets = presetManager.listPresets(ctx)
                if (presets.isNotEmpty()) {
                    val presetIndex = event.number % presets.size
                    val presetName = presets[presetIndex]
                    Log.i(TAG, "Preset switch: loading '$presetName' (#$presetIndex)")
                    val engine = NativeEngine.getInstance()
                    engine.setChainBypass(true)
                    try {
                        presetManager.loadPreset(ctx, presetName)
                    } finally {
                        engine.setChainBypass(false)
                    }
                }
            }
            MidiActionType.PRESET_NEXT -> {
                if (event.value > 0) {
                    val ctx = appContext ?: return
                    val presets = presetManager.listPresets(ctx)
                    if (presets.isNotEmpty()) {
                        currentPresetIndex = (currentPresetIndex + 1) % presets.size
                        val presetName = presets[currentPresetIndex]
                        Log.i(TAG, "Preset NEXT: loading '$presetName'")
                        val engine = NativeEngine.getInstance()
                        engine.setChainBypass(true)
                        try {
                            presetManager.loadPreset(ctx, presetName)
                        } finally {
                            engine.setChainBypass(false)
                        }
                    }
                }
            }
            MidiActionType.PRESET_PREV -> {
                if (event.value > 0) {
                    val ctx = appContext ?: return
                    val presets = presetManager.listPresets(ctx)
                    if (presets.isNotEmpty()) {
                        currentPresetIndex = if (currentPresetIndex <= 0) presets.size - 1 else currentPresetIndex - 1
                        val presetName = presets[currentPresetIndex]
                        Log.i(TAG, "Preset PREV: loading '$presetName'")
                        val engine = NativeEngine.getInstance()
                        engine.setChainBypass(true)
                        try {
                            presetManager.loadPreset(ctx, presetName)
                        } finally {
                            engine.setChainBypass(false)
                        }
                    }
                }
            }
        }
    }

    /**
     * Send CC message back to controller (e.g. to illuminate Nektar Pacer LEDs).
     */
    private fun sendFeedbackCc(ccNumber: Int, value: Int) {
        val port = activeFeedbackPort ?: return
        try {
            val msg = byteArrayOf(0xB0.toByte(), ccNumber.toByte(), value.toByte())
            port.send(msg, 0, msg.size)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to send MIDI feedback: ${e.message}")
        }
    }
}
