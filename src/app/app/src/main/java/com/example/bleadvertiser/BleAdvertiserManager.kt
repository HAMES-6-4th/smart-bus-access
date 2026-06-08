package com.example.bleadvertiser

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.bluetooth.le.BluetoothLeAdvertiser
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat

class BleAdvertiserManager(private val context: Context) {

    private val bluetoothAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
    private val advertiser: BluetoothLeAdvertiser? = bluetoothAdapter?.bluetoothLeAdvertiser

    private var isAdvertising = false

    private val advertiseCallback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings) {
            isAdvertising = true
        }

        override fun onStartFailure(errorCode: Int) {
            isAdvertising = false
        }
    }

    private fun hasAdvertisePermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.BLUETOOTH_ADVERTISE
            ) == PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
    }

    fun isSupported(): Boolean {
        val adapter = bluetoothAdapter ?: return false
        return adapter.isEnabled &&
                adapter.isMultipleAdvertisementSupported &&
                advertiser != null
    }

    fun isAdvertising(): Boolean = isAdvertising

    fun startSlopeRequestAdvertise(): Boolean {
        if (!isSupported()) return false
        if (!hasAdvertisePermission()) return false
        if (isAdvertising) return true

        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setConnectable(false)
            .build()

        val manufacturerData = byteArrayOf(
            0xA1.toByte(), // slope request
            0x01.toByte()  // active
        )

        val data = AdvertiseData.Builder()
            .setIncludeDeviceName(false)
            .setIncludeTxPowerLevel(false)
            .addManufacturerData(0x1234, manufacturerData)
            .build()

        return try {
            advertiser?.startAdvertising(settings, data, advertiseCallback)
            true
        } catch (e: SecurityException) {
            isAdvertising = false
            false
        }
    }

    fun stopAdvertise() {
        if (!hasAdvertisePermission()) {
            isAdvertising = false
            return
        }

        try {
            advertiser?.stopAdvertising(advertiseCallback)
        } catch (_: SecurityException) {
        } finally {
            isAdvertising = false
        }
    }
}