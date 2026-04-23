# AGENTS.md: Senseware Implementation Plan

## System Directive
**Target Hardware:** YD-ESP32 Type-A (ESP32-WROOM-32, FQBN: esp32:esp32:dfrobot_firebeetle2_esp32e, flashed via DFRobot FireBeetle 2 ESP32-E profile)
**Core Components:** MAX30102 (PPG), MPU-9250 (IMU), OYMotion Analog EMG, SH1106 OLED, LRA Vibration Motor.
**Objective:** Execute the staged development of an autonomous, edge-AI physiological monitoring wearable.

---

## Phase 0: WSL Development Environment Initialization
**Goal:** Establish a unified, CLI-driven toolchain in WSL for ESP32 compilation and flashing, utilizing USB passthrough from the Windows host.

1. **Toolchain Setup:** Install and configure `arduino-cli` within the WSL environment.
2. **Core Configuration:** Add the Espressif ESP32 package index (`https://espressif.github.io/arduino-esp32/package_esp32_index.json`) and install the `esp32:esp32` core.
3. **Library Dependencies:** Execute `arduino-cli lib install` for the following:
   - `"SparkFun MAX3010x Pulse and Proximity Sensor Library"`
   - `"MPU9250"`
   - `"Adafruit SH110X"`
   - `"Adafruit NeoPixel"`
4. **Hardware Handshake:** The human user will execute `usbipd` on the Windows host to bind the ESP32 board's serial port to WSL. The agent must verify the device presence (e.g., via `lsusb` or checking `/dev/ttyUSB*` / `/dev/ttyACM*`) prior to executing compile and upload commands.

---

## Phase 1: The Core Data Pipeline (RTOS & Signal Processing)
**Goal:** Establish a flawless, timestamped stream of synchronized physiological data without blocking the main loop.

1. **RTOS Architecture:** Implement FreeRTOS tasks. Create one high-frequency task for polling the EMG (analog GPIO 34) and a separate task driven by hardware interrupts to pull data from the MAX30102 (I2C) and MPU-9250 (I2C) FIFO buffers.
   - **EMG:** Uses the official OYMotion EMGFilters library (bandpass 20–150 Hz, 50 Hz notch) sampling at exactly 1000 Hz.
2. **Signal Integration:** Incorporate the raw sensor interpretation logic. Convert raw ADC values and PPG waveforms into smoothed variables (e.g., `current_HR`, `current_EMG_envelope`).
3. **Data Struct:** Define a C++ `struct` to hold a unified snapshot of the user's current state (Heart Rate, EMG Tension, IMU Motion).
4. **Validation:** Output this struct to the serial port and display the live metrics on the SH1106 OLED.

---

## Phase 2: Data Collection (Baseline Logging)
**Goal:** Capture high-fidelity "calm" baseline data to train the anomaly detection model.

1. **Logging Firmware:** Modify the Phase 1 firmware to output the synchronized data struct over the serial port in a strict CSV format (e.g., `millis,heart_rate,spo2,emg_envelope,motion_magnitude,alert`).
2. **Data Capture:** Instruct the user to run a Python logging script (using `pyserial`) in WSL to capture this serial stream to a `.csv` file during a controlled, relaxed 15-30 minute session.

---

## Phase 3: Model Training (Off-Device)
**Goal:** Train an autoencoder to recognize the user's baseline allostatic state.

1. **Neural Network Design:** Using Python (TensorFlow/Keras) in WSL, construct a lightweight autoencoder (e.g., Input -> Dense(16) -> Dense(8, Latent) -> Dense(16) -> Output).
2. **Training & Thresholding:** Train the model exclusively on the baseline CSV data. Inject artificial stress anomalies into a validation set to determine the Mean Squared Error (MSE) threshold that constitutes an "allostatic spike."
3. **Model Export:** Convert the Keras model into a TensorFlow Lite `.tflite` C-byte array header file.

---

## Phase 4: Edge AI Integration
**Goal:** Run the trained model natively on the ESP32 to trigger closed-loop haptic interventions.

1. **Library Integration:** Install and include the `Chirale_TensorFlowLite` library (TFLite Micro C++ API). Import the `.tflite` model header.
   - **Heart Rate:** Uses the SparkFun MAX30105 library with heartRate.h for software beat detection at 25Hz polling rate. SpO2 computed from red/IR DC ratio (Maxim AN6407 formula).
   - Note: The DFRobot SEN0344 module and DFRobot_BloodOxygen_S library were replaced with a bare MAX30102 chip driven directly by the SparkFun MAX3010x library. The DFRobot module's internal MCU was not reliably driving the PPG LED when co-located with other I2C devices.
2. **Inference Task:** Create a new FreeRTOS task. At set intervals, this task normalizes the latest Phase 1 data struct, feeds it into the autoencoder, and calculates the reconstruction error.
   - **Adaptive Threshold:** The system learns the user's baseline reconstruction error over time, continuously adjusting the anomaly detection threshold.
3. **Closed-Loop Trigger:** If the reconstruction error exceeds the established threshold, trigger the native ESP32 v3.x `ledcWrite()` functions to fire the haptic motor (GPIO 25) and update the OLED with an intervention prompt.

---

## Phase 5: WiFi Communication (HTTP + SSE)
**Goal:** Transmit telemetry and intervention alerts without maintaining a power-hungry Wi-Fi connection beyond the persistent SSE stream.

1. **HTTP Server:** Utilize the ESP32's native WiFi library to start an HTTP server on port 81.
2. **SSE Endpoint:** Implement a `GET /events` route that holds a persistent HTTP connection and pushes JSON telemetry every 1 second using the Server-Sent Events protocol.
3. **Payload:** Push a 16-byte JSON payload per tick: `{"hr":72.0,"spo2":98.0,"emg":50.0,"mot":1.03,"mse":0.0005,"acc":95.0,"anomaly":0}`.
4. **Keepalive:** Send SSE comment lines (`: keepalive`) every 3 seconds to prevent connection timeout.
5. **REST Endpoint:** Provide a `GET /telemetry` single-shot JSON endpoint for on-demand queries.

---

## Phase 6: Caregiver Dashboard (Vue.js)
**Goal:** Visualize live data and log interventions remotely via any modern browser.

1. **Web App Construction:** Build a Vue 3 dashboard using Vite, connecting to the ESP32 via the EventSource (SSE) API.
2. **Real-Time Visualization:** Subscribe to the SSE `/events` stream to chart physiological data (HR, SpO2, EMG, Motion) on a rolling Chart.js graph with separate y-axes.
3. **Alert System:** Rising-edge detection on the `anomaly` field triggers caregiver alerts and logs intervention events with timestamps.
4. **Connection Resilience:** Debounced reconnect logic prevents UI flicker during brief network blips. Silent reconnection within a 2-second window.
