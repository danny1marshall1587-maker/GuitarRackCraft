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

package com.varcain.guitarrackcraft.ui.midi

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.varcain.guitarrackcraft.midi.MidiActionType
import com.varcain.guitarrackcraft.midi.MidiControllerManager
import com.varcain.guitarrackcraft.midi.MidiMapping
import com.varcain.guitarrackcraft.midi.MidiMappingManager
import com.varcain.guitarrackcraft.midi.MidiMessageType

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MidiSettingsDialog(
    onDismissRequest: () -> Unit
) {
    val context = LocalContext.current
    val connectedDevice by MidiControllerManager.connectedDeviceName.collectAsState()
    val lastEvent by MidiControllerManager.lastMidiEvent.collectAsState()
    val activityTimestamp by MidiControllerManager.midiActivityTimestamp.collectAsState()
    val mappings by MidiMappingManager.mappings.collectAsState()
    val isLearning by MidiMappingManager.isLearning.collectAsState()

    var showAddDialog by remember { mutableStateOf(false) }

    Dialog(
        onDismissRequest = onDismissRequest,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.95f)
                .fillMaxHeight(0.85f),
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 8.dp
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(20.dp)
            ) {
                // Header
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "MIDI Controller Setup",
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = "Nektar Pacer & USB MIDI Controls",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    IconButton(onClick = onDismissRequest) {
                        Icon(Icons.Default.Close, contentDescription = "Close")
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Device Connection Status Card
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                    ),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            val isConnected = connectedDevice != null
                            Box(
                                modifier = Modifier
                                    .size(12.dp)
                                    .clip(CircleShape)
                                    .background(if (isConnected) Color(0xFF4CAF50) else Color(0xFFFF9800))
                            )
                            Spacer(modifier = Modifier.width(10.dp))
                            Column {
                                Text(
                                    text = connectedDevice ?: "No USB MIDI Device Detected",
                                    fontWeight = FontWeight.SemiBold,
                                    fontSize = 14.sp
                                )
                                Text(
                                    text = if (isConnected) "Active • Ready for Nektar Pacer" else "Plug in Nektar Pacer via USB-C OTG Hub",
                                    fontSize = 12.sp,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }

                        // MIDI Activity monitor
                        lastEvent?.let { ev ->
                            val isRecent = System.currentTimeMillis() - activityTimestamp < 1500
                            Surface(
                                shape = RoundedCornerShape(8.dp),
                                color = if (isRecent) Color(0xFF2E7D32) else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f)
                            ) {
                                Text(
                                    text = "${ev.messageType} #${ev.number} (${ev.value})",
                                    color = Color.White,
                                    fontSize = 11.sp,
                                    fontWeight = FontWeight.Medium,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                                )
                            }
                        }
                    }
                }

                // MIDI Learn Active Banner
                if (isLearning) {
                    Spacer(modifier = Modifier.height(10.dp))
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
                        border = CardDefaults.outlinedCardBorder().copy(brush = androidx.compose.ui.graphics.SolidColor(MaterialTheme.colorScheme.primary))
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(12.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = "MIDI LEARN ACTIVE",
                                    fontWeight = FontWeight.Bold,
                                    fontSize = 13.sp,
                                    color = MaterialTheme.colorScheme.onPrimaryContainer
                                )
                                Text(
                                    text = "Press any footswitch or move pedal on Pacer to map...",
                                    fontSize = 12.sp,
                                    color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                                )
                            }
                            TextButton(
                                onClick = { MidiMappingManager.cancelLearn() }
                            ) {
                                Text("Cancel")
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Mappings List Header & Actions
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Mapped Controls (${mappings.size})",
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold
                    )
                    Row {
                        TextButton(
                            onClick = { MidiMappingManager.loadDefaultPacerMappings(context) }
                        ) {
                            Icon(Icons.Default.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(modifier = Modifier.width(4.dp))
                            Text("Reset Pacer Defaults", fontSize = 12.sp)
                        }
                        Spacer(modifier = Modifier.width(4.dp))
                        FilledTonalButton(
                            onClick = { showAddDialog = true }
                        ) {
                            Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(modifier = Modifier.width(4.dp))
                            Text("Add", fontSize = 12.sp)
                        }
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                // Mappings List
                LazyColumn(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    items(mappings, key = { it.id }) { mapping ->
                        MappingCard(
                            mapping = mapping,
                            isLearning = isLearning,
                            onLearn = { MidiMappingManager.startLearn(mapping) },
                            onDelete = { MidiMappingManager.removeMapping(context, mapping.id) }
                        )
                    }
                }
            }
        }
    }

    if (showAddDialog) {
        AddMappingDialog(
            onDismiss = { showAddDialog = false },
            onAdd = { newMapping ->
                MidiMappingManager.addOrUpdateMapping(context, newMapping)
                showAddDialog = false
            }
        )
    }
}

@Composable
private fun MappingCard(
    mapping: MidiMapping,
    isLearning: Boolean,
    onLearn: () -> Unit,
    onDelete: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(10.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = mapping.name,
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 14.sp
                )
                Spacer(modifier = Modifier.height(2.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Surface(
                        shape = RoundedCornerShape(4.dp),
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                    ) {
                        Text(
                            text = "${mapping.messageType} #${mapping.controlNumber}",
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp)
                        )
                    }
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = "• ${mapping.actionType.displayName}" +
                                if (mapping.actionType == MidiActionType.SLOT_BYPASS_TOGGLE || mapping.actionType == MidiActionType.PARAMETER_CONTROL)
                                    " (Slot ${mapping.targetSlotIndex + 1})" else "",
                        fontSize = 12.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedButton(
                    onClick = onLearn,
                    enabled = !isLearning,
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 4.dp),
                    modifier = Modifier.height(32.dp)
                ) {
                    Text("Learn", fontSize = 12.sp)
                }
                Spacer(modifier = Modifier.width(4.dp))
                IconButton(onClick = onDelete) {
                    Icon(
                        Icons.Default.Delete,
                        contentDescription = "Delete",
                        tint = MaterialTheme.colorScheme.error.copy(alpha = 0.8f),
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddMappingDialog(
    onDismiss: () -> Unit,
    onAdd: (MidiMapping) -> Unit
) {
    var name by remember { mutableStateOf("New Control") }
    var ccNumber by remember { mutableIntStateOf(21) }
    var actionType by remember { mutableStateOf(MidiActionType.SLOT_BYPASS_TOGGLE) }
    var targetSlot by remember { mutableIntStateOf(0) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Add MIDI Mapping") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Mapping Name") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                OutlinedTextField(
                    value = ccNumber.toString(),
                    onValueChange = { ccNumber = it.toIntOrNull() ?: ccNumber },
                    label = { Text("MIDI CC Number (0-127)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                OutlinedTextField(
                    value = (targetSlot + 1).toString(),
                    onValueChange = { targetSlot = ((it.toIntOrNull() ?: 1) - 1).coerceAtLeast(0) },
                    label = { Text("Target Rack Slot (1-based)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = {
            Button(
                onClick = {
                    onAdd(
                        MidiMapping(
                            name = name,
                            controlNumber = ccNumber,
                            actionType = actionType,
                            targetSlotIndex = targetSlot
                        )
                    )
                }
            ) {
                Text("Add Mapping")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SliderMidiLearnDialog(
    slotIndex: Int,
    port: com.varcain.guitarrackcraft.engine.PortInfo,
    onDismissRequest: () -> Unit
) {
    val context = LocalContext.current
    val allMappings by MidiMappingManager.mappings.collectAsState()
    val isLearning by MidiMappingManager.isLearning.collectAsState()

    val paramMappings = remember(allMappings, slotIndex, port.index) {
        allMappings.filter {
            it.actionType == MidiActionType.PARAMETER_CONTROL &&
            it.targetSlotIndex == slotIndex &&
            it.targetPortIndex == port.index
        }
    }

    var editingMappingId by remember { mutableStateOf<String?>(null) }
    var ccNumber by remember { mutableIntStateOf(11) }
    var minValStr by remember { mutableStateOf("%.2f".format(port.minValue)) }
    var maxValStr by remember { mutableStateOf("%.2f".format(port.maxValue)) }

    LaunchedEffect(paramMappings) {
        if (paramMappings.isNotEmpty() && editingMappingId == null) {
            val first = paramMappings.first()
            editingMappingId = first.id
            ccNumber = first.controlNumber
            minValStr = "%.2f".format(first.minVal)
            maxValStr = "%.2f".format(first.maxVal)
        }
    }

    Dialog(
        onDismissRequest = {
            if (isLearning) MidiMappingManager.cancelLearn()
            onDismissRequest()
        },
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.92f)
                .wrapContentHeight(),
            shape = RoundedCornerShape(20.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 10.dp
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(20.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "MIDI Learn: " + (if (port.name.isNotEmpty()) port.name else port.symbol),
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            text = "Slot " + (slotIndex + 1) + " • Range: " + "%.2f".format(port.minValue) + " to " + "%.2f".format(port.maxValue),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    IconButton(onClick = {
                        if (isLearning) MidiMappingManager.cancelLearn()
                        onDismissRequest()
                    }) {
                        Icon(Icons.Default.Close, contentDescription = "Close")
                    }
                }

                Spacer(modifier = Modifier.height(14.dp))

                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = if (isLearning)
                            MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.6f)
                        else
                            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                    ),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Column {
                            Text(
                                text = if (isLearning) "WAITING FOR MIDI..." else "MIDI CC ASSIGNMENT",
                                fontSize = 11.sp,
                                fontWeight = FontWeight.Bold,
                                color = if (isLearning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = if (isLearning)
                                    "Move expression pedal or footswitch now"
                                else
                                    "Assigned to CC #" + ccNumber + " (Channel: Omni)",
                                fontSize = 12.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }

                        Button(
                            onClick = {
                                val minF = minValStr.toFloatOrNull() ?: port.minValue
                                val maxF = maxValStr.toFloatOrNull() ?: port.maxValue
                                val target = MidiMapping(
                                    id = editingMappingId ?: java.util.UUID.randomUUID().toString(),
                                    name = "Slot " + (slotIndex + 1) + " " + port.name,
                                    messageType = MidiMessageType.CC,
                                    controlNumber = ccNumber,
                                    actionType = MidiActionType.PARAMETER_CONTROL,
                                    targetSlotIndex = slotIndex,
                                    targetPortIndex = port.index,
                                    minVal = minF,
                                    maxVal = maxF
                                )
                                if (isLearning) {
                                    MidiMappingManager.cancelLearn()
                                } else {
                                    MidiMappingManager.startLearn(target)
                                }
                            },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (isLearning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                            )
                        ) {
                            Text(if (isLearning) "Cancel" else "MIDI Learn")
                        }
                    }
                }

                Spacer(modifier = Modifier.height(14.dp))

                Text(
                    text = "Control Value Range (Supports Inversion)",
                    style = MaterialTheme.typography.labelMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = "Set Min higher than Max to reverse controller direction (Heel = High, Toe = Low).",
                    fontSize = 11.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )

                Spacer(modifier = Modifier.height(8.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    OutlinedTextField(
                        value = minValStr,
                        onValueChange = { minValStr = it },
                        label = { Text("Min (CC 0)") },
                        singleLine = true,
                        modifier = Modifier.weight(1f)
                    )

                    OutlinedButton(
                        onClick = {
                            val temp = minValStr
                            minValStr = maxValStr
                            maxValStr = temp
                        },
                        contentPadding = PaddingValues(horizontal = 8.dp)
                    ) {
                        Text("⇄ Invert", fontSize = 12.sp)
                    }

                    OutlinedTextField(
                        value = maxValStr,
                        onValueChange = { maxValStr = it },
                        label = { Text("Max (CC 127)") },
                        singleLine = true,
                        modifier = Modifier.weight(1f)
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))

                OutlinedTextField(
                    value = ccNumber.toString(),
                    onValueChange = { ccNumber = it.toIntOrNull()?.coerceIn(0, 127) ?: ccNumber },
                    label = { Text("MIDI CC Number (0-127)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                Spacer(modifier = Modifier.height(16.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    if (editingMappingId != null) {
                        OutlinedButton(
                            onClick = {
                                editingMappingId?.let { id ->
                                    MidiMappingManager.removeMapping(context, id)
                                    editingMappingId = null
                                }
                            },
                            colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error)
                        ) {
                            Icon(Icons.Default.Delete, contentDescription = "Delete", modifier = Modifier.size(16.dp))
                            Spacer(modifier = Modifier.width(4.dp))
                            Text("Unbind")
                        }
                    }

                    Spacer(modifier = Modifier.weight(1f))

                    Button(
                        onClick = {
                            val minF = minValStr.toFloatOrNull() ?: port.minValue
                            val maxF = maxValStr.toFloatOrNull() ?: port.maxValue
                            val mapping = MidiMapping(
                                id = editingMappingId ?: java.util.UUID.randomUUID().toString(),
                                name = "Slot " + (slotIndex + 1) + " " + port.name,
                                messageType = MidiMessageType.CC,
                                controlNumber = ccNumber,
                                actionType = MidiActionType.PARAMETER_CONTROL,
                                targetSlotIndex = slotIndex,
                                targetPortIndex = port.index,
                                minVal = minF,
                                maxVal = maxF
                            )
                            MidiMappingManager.addOrUpdateMapping(context, mapping)
                            if (isLearning) MidiMappingManager.cancelLearn()
                            onDismissRequest()
                        }
                    ) {
                        Text(if (editingMappingId != null) "Save" else "Assign CC")
                    }
                }

                if (paramMappings.size > 1) {
                    Spacer(modifier = Modifier.height(12.dp))
                    Divider()
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Active Assignments for this parameter:",
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    paramMappings.forEach { m ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 4.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "• CC #" + m.controlNumber + ": Range [" + "%.2f".format(m.minVal) + " -> " + "%.2f".format(m.maxVal) + "]",
                                fontSize = 11.sp,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            IconButton(
                                onClick = { MidiMappingManager.removeMapping(context, m.id) },
                                modifier = Modifier.size(24.dp)
                            ) {
                                Icon(Icons.Default.Delete, contentDescription = "Remove", tint = MaterialTheme.colorScheme.error, modifier = Modifier.size(14.dp))
                            }
                        }
                    }
                }
            }
        }
    }
}
