package com.dualcare.app

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.firebase.database.ktx.database
import com.google.firebase.ktx.Firebase

class MainActivity : AppCompatActivity(), SensorEventListener {
    private lateinit var sensorManager: SensorManager
    private var accelerometer: Sensor? = null
    private val dbRef = Firebase.database.getReference("sensors/accelerometer")
    private lateinit var tvStatus: TextView
    private lateinit var btnToggle: Button
    private var streaming = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        tvStatus = findViewById(R.id.tvStatus)
        btnToggle = findViewById(R.id.btnToggle)
        sensorManager = getSystemService(SENSOR_SERVICE) as SensorManager
        accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)

        btnToggle.setOnClickListener {
            streaming = !streaming
            if (streaming) {
                sensorManager.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_NORMAL)
                tvStatus.text = "Streaming"
            } else {
                sensorManager.unregisterListener(this)
                tvStatus.text = "Stopped"
            }
        }
    }

    override fun onSensorChanged(event: SensorEvent?) {
        event ?: return
        val data = mapOf(
            "timestamp" to System.currentTimeMillis(),
            "x" to event.values[0],
            "y" to event.values[1],
            "z" to event.values[2]
        )
        dbRef.push().setValue(data)
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    override fun onDestroy() {
        sensorManager.unregisterListener(this)
        super.onDestroy()
    }
}
