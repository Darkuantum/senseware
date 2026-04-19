# senseware

Autonomous, edge-AI physiological monitoring wearable built on the **DFRobot FireBeetle 2 ESP32-E**.

## Hardware

| Component | Part |
|---|---|
| MCU | DFRobot FireBeetle 2 ESP32-E |
| PPG Heart Rate | MAX30102 (I2C) |
| IMU | MPU-9250 (I2C) |
| EMG | OYMotion Analog EMG (ADC GPIO 34) |
| Display | SH1106 OLED (I2C) |
| Haptic | LRA Vibration Motor (GPIO 25, PWM) |

## Architecture

The firmware runs a FreeRTOS-based data pipeline that synchronizes sensor streams into a unified state struct. An on-device autoencoder (TFLite Micro) performs real-time allostatic load anomaly detection, triggering closed-loop haptic interventions when stress spikes are detected. Telemetry and alerts are pushed over WiFi via HTTP Server-Sent Events (SSE) to a caregiver dashboard built with Vue 3.

## Project Structure

```
senseware/
├── senseware_code/          # Main Arduino firmware
│   └── senseware_code.ino
├── EMG_Filter/              # EMG signal processing library
│   └── EMGFilters/
├── python/                  # Data collection & model training scripts
├── dashboard/               # Caregiver web dashboard (Vue 3 + SSE)
├── models/                  # Trained TFLite models (gitignored)
└── AGENTS.md                # Full phased implementation plan
```

## Development Phases

0. **Environment Setup** -- WSL toolchain (arduino-cli, USB passthrough)
1. **Core Pipeline** -- FreeRTOS tasks, sensor fusion, OLED output
2. **Baseline Logging** -- CSV data capture for model training
3. **Model Training** -- Autoencoder (Keras) -> TFLite C header
4. **Edge AI** -- On-device inference + haptic closed loop
5. **WiFi + SSE** -- HTTP Server-Sent Events telemetry & keepalive
6. **Dashboard** -- Vue 3 caregiver UI (Chart.js, rising-edge alerts)

See [AGENTS.md](AGENTS.md) for the complete implementation plan.
