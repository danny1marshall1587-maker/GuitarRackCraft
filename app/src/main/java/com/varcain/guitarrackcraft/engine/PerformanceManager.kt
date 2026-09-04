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

package com.varcain.guitarrackcraft.engine

import android.app.Activity
import android.content.Context
import android.os.Build
import android.os.PerformanceHintManager
import android.os.PowerManager
import android.util.Log
import android.view.Window
import android.view.WindowManager
import java.lang.ref.WeakReference

/**
 * Manages CPU performance policies, anti-throttling mechanisms, ADPF hints,
 * wake locks, and sustained performance mode on Google Pixel / Tensor and modern Android devices.
 */
object PerformanceManager {

    private const val TAG = "PerformanceManager"
    private const val WAKE_LOCK_TAG = "GuitarRackCraft:AudioRunning"

    private var wakeLock: PowerManager.WakeLock? = null
    private var activityRef: WeakReference<Activity>? = null
    private var adpfSession: PerformanceHintManager.Session? = null

    /**
     * Attach the active Activity window to the manager.
     */
    fun attachActivity(activity: Activity) {
        activityRef = WeakReference(activity)
        Log.i(TAG, "Activity attached to PerformanceManager")
    }

    /**
     * Detach the activity window.
     */
    fun detachActivity() {
        activityRef = null
    }

    /**
     * Enter high-performance anti-throttling audio mode:
     * - Acquires a PARTIAL_WAKE_LOCK so CPU never dozes or idles when screen is locked/dark.
     * - Enables Window FLAG_KEEP_SCREEN_ON so screen does not dim or sleep during live play.
     * - Enables Window sustained performance mode (hardware supported on Google Pixel).
     * - Configures ADPF (Android Dynamic Performance Framework) on API 31+ for the AAudio thread.
     */
    fun onAudioStreamStarted(context: Context, sampleRate: Float = 48000f, bufferFrames: Int = 128) {
        try {
            acquireWakeLock(context)
            enableScreenKeepOn()
            enableSustainedPerformance(context)
            setupAdpfSession(context, sampleRate, bufferFrames)
            Log.i(TAG, "Entered high-performance audio streaming mode")
        } catch (e: Exception) {
            Log.e(TAG, "Error configuring performance mode: ${e.message}", e)
        }
    }

    /**
     * Exit high-performance mode when audio engine stops.
     */
    fun onAudioStreamStopped() {
        try {
            releaseWakeLock()
            disableScreenKeepOn()
            disableSustainedPerformance()
            closeAdpfSession()
            Log.i(TAG, "Exited high-performance audio streaming mode")
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping performance mode: ${e.message}", e)
        }
    }

    private fun acquireWakeLock(context: Context) {
        if (wakeLock == null) {
            val pm = context.applicationContext.getSystemService(Context.POWER_SERVICE) as? PowerManager
            wakeLock = pm?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, WAKE_LOCK_TAG)?.apply {
                setReferenceCounted(false)
            }
        }
        wakeLock?.let {
            if (!it.isHeld) {
                it.acquire(12 * 60 * 60 * 1000L) // 12 hour max safety timeout
                Log.i(TAG, "PowerManager PARTIAL_WAKE_LOCK acquired")
            }
        }
    }

    private fun releaseWakeLock() {
        wakeLock?.let {
            if (it.isHeld) {
                it.release()
                Log.i(TAG, "PowerManager PARTIAL_WAKE_LOCK released")
            }
        }
    }

    private fun enableScreenKeepOn() {
        activityRef?.get()?.runOnUiThread {
            try {
                activityRef?.get()?.window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                Log.i(TAG, "FLAG_KEEP_SCREEN_ON enabled")
            } catch (e: Exception) {
                Log.w(TAG, "Could not set FLAG_KEEP_SCREEN_ON: ${e.message}")
            }
        }
    }

    private fun disableScreenKeepOn() {
        activityRef?.get()?.runOnUiThread {
            try {
                activityRef?.get()?.window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                Log.i(TAG, "FLAG_KEEP_SCREEN_ON cleared")
            } catch (e: Exception) {
                Log.w(TAG, "Could not clear FLAG_KEEP_SCREEN_ON: ${e.message}")
            }
        }
    }

    private fun enableSustainedPerformance(context: Context) {
        val activity = activityRef?.get() ?: return
        activity.runOnUiThread {
            try {
                val pm = context.applicationContext.getSystemService(Context.POWER_SERVICE) as? PowerManager
                val isSupported = pm?.isSustainedPerformanceModeSupported ?: false
                if (isSupported) {
                    activity.window?.setSustainedPerformanceMode(true)
                    Log.i(TAG, "Window sustained performance mode ENABLED (Pixel/Tensor hardware supported)")
                } else {
                    Log.i(TAG, "Sustained performance mode not supported on this device")
                }
            } catch (e: Exception) {
                Log.w(TAG, "Could not enable sustained performance mode: ${e.message}")
            }
        }
    }

    private fun disableSustainedPerformance() {
        val activity = activityRef?.get() ?: return
        activity.runOnUiThread {
            try {
                activity.window?.setSustainedPerformanceMode(false)
                Log.i(TAG, "Window sustained performance mode DISABLED")
            } catch (e: Exception) {
                Log.w(TAG, "Could not disable sustained performance mode: ${e.message}")
            }
        }
    }

    private fun setupAdpfSession(context: Context, sampleRate: Float, bufferFrames: Int) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                val phm = context.applicationContext.getSystemService(PerformanceHintManager::class.java)
                if (phm == null) {
                    Log.i(TAG, "ADPF PerformanceHintManager not available on this device")
                    return
                }

                // Query audio callback thread ID from native engine
                val audioTid = NativeEngine.getInstance().getAudioThreadTid()
                val tids = if (audioTid > 0) {
                    intArrayOf(audioTid)
                } else {
                    intArrayOf(android.os.Process.myTid())
                }

                val frames = if (bufferFrames > 0) bufferFrames else 128
                val rate = if (sampleRate > 0) sampleRate else 48000f
                val targetDurationNanos = ((frames.toDouble() / rate) * 1_000_000_000.0).toLong()

                closeAdpfSession()
                adpfSession = phm.createHintSession(tids, targetDurationNanos)
                Log.i(TAG, "ADPF PerformanceHintManager Session created for TID=${tids.joinToString()} target=${targetDurationNanos / 1_000_000.0}ms")
            } catch (e: Exception) {
                Log.w(TAG, "ADPF PerformanceHintManager session setup failed: ${e.message}")
            }
        }
    }

    /**
     * Report actual DSP work duration to the ADPF governor.
     */
    fun reportWorkDuration(durationNanos: Long) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                adpfSession?.reportActualWorkDuration(durationNanos)
            } catch (_: Exception) {}
        }
    }

    private fun closeAdpfSession() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                adpfSession?.close()
                adpfSession = null
            } catch (_: Exception) {}
        }
    }
}
