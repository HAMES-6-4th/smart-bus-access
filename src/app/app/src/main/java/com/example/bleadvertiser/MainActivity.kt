package com.example.bleadvertiser

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.CountDownTimer
import android.view.View
import android.widget.Button
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {

    private lateinit var btnBeacon: Button
    private lateinit var glowView: View

    private lateinit var bleAdvertiserManager: BleAdvertiserManager
    private var countDownTimer: CountDownTimer? = null
    private var isActive = false

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { result ->
            val advertiseGranted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                result[Manifest.permission.BLUETOOTH_ADVERTISE] == true
            } else {
                true
            }

            val connectGranted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                result[Manifest.permission.BLUETOOTH_CONNECT] == true
            } else {
                true
            }

            if (advertiseGranted && connectGranted) {
                startBeaconMode()
            } else {
                Toast.makeText(this, "블루투스 권한이 필요합니다.", Toast.LENGTH_SHORT).show()
                updateInactiveUi()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        btnBeacon = findViewById(R.id.btnBeacon)
        glowView = findViewById(R.id.glowView)

        bleAdvertiserManager = BleAdvertiserManager(this)

        updateInactiveUi()

        btnBeacon.setOnClickListener {
            if (isActive) {
                stopBeaconMode("즉시 중지")
            } else {
                ensurePermissionAndStart()
            }
        }
    }

    private fun ensurePermissionAndStart() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val advertiseGranted = ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.BLUETOOTH_ADVERTISE
            ) == PackageManager.PERMISSION_GRANTED

            val connectGranted = ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.BLUETOOTH_CONNECT
            ) == PackageManager.PERMISSION_GRANTED

            if (advertiseGranted && connectGranted) {
                startBeaconMode()
            } else {
                permissionLauncher.launch(
                    arrayOf(
                        Manifest.permission.BLUETOOTH_ADVERTISE,
                        Manifest.permission.BLUETOOTH_CONNECT
                    )
                )
            }
        } else {
            startBeaconMode()
        }
    }

    private fun startBeaconMode() {
        if (!bleAdvertiserManager.isSupported()) {
            Toast.makeText(
                this,
                "이 기기는 BLE 광고를 지원하지 않거나 Bluetooth가 꺼져 있습니다.",
                Toast.LENGTH_LONG
            ).show()
            updateInactiveUi()
            return
        }

        val started = bleAdvertiserManager.startSlopeRequestAdvertise()
        if (!started) {
            Toast.makeText(this, "광고 시작 실패", Toast.LENGTH_SHORT).show()
            updateInactiveUi()
            return
        }

        isActive = true
        updateActiveUi(60)

        countDownTimer?.cancel()
        countDownTimer = object : CountDownTimer(60_000, 1_000) {
            override fun onTick(millisUntilFinished: Long) {
                val secondsLeft = (millisUntilFinished / 1000).toInt()
                updateActiveUi(secondsLeft)
            }

            override fun onFinish() {
                stopBeaconMode("60초 후 자동 종료")
            }
        }.start()
    }

    private fun stopBeaconMode(message: String? = null) {
        countDownTimer?.cancel()
        countDownTimer = null

        bleAdvertiserManager.stopAdvertise()
        isActive = false
        updateInactiveUi()

        if (message != null) {
            Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
        }
    }

    private fun updateInactiveUi() {
        btnBeacon.background = ContextCompat.getDrawable(this, R.drawable.circle_button_inactive)
        btnBeacon.setTextColor(ContextCompat.getColor(this, android.R.color.white))
        btnBeacon.text = "슬로프\n요청"
        glowView.visibility = View.GONE
    }

    private fun updateActiveUi(secondsLeft: Int) {
        btnBeacon.background = ContextCompat.getDrawable(this, R.drawable.circle_button_active)
        btnBeacon.setTextColor(0xFF39FF88.toInt())
        btnBeacon.text = "활성화!\n(${secondsLeft}초)"
        glowView.visibility = View.VISIBLE
    }

    override fun onDestroy() {
        countDownTimer?.cancel()
        bleAdvertiserManager.stopAdvertise()
        super.onDestroy()
    }
}