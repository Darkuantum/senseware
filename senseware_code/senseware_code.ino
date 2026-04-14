/*
 * Senseware Firmware — Phase 5 (MAJOR REWRITE)
 * Target: DFRobot FireBeetle 2 ESP32-E (esp32:esp32:firebeetle32)
 *
 * Sensors:
 *   DFRobot SEN0344 Blood Oxygen (HR + SpO2) via DFRobot_BloodOxygen_S
 *   MPU-9250 9-DOF IMU
 *   OYMotion Analog EMG with EMGFilters (1000 Hz bandpass)
 *   SH1106 OLED 128x64
 *   LRA Vibration Motor via LEDC PWM
 *
 * Edge AI: TFLite Micro autoencoder for anomaly detection
 * BLE: GATT server with telemetry + alert characteristics
 *
 * Major changes from prior version:
 *   - Replaced raw MAX30105 with DFRobot_BloodOxygen_S (real BPM/SpO2)
 *   - Replaced naive EMG rectifier with EMGFilters (proper 20-150Hz bandpass)
 *   - EMG sampling at exactly 1000Hz via vTaskDelayUntil
 *   - HR reads are interrupt-driven from SEN0344 INT pin
 *   - Adaptive threshold with NVS persistence (Preferences)
 *   - Non-blocking haptic with cooldown
 *   - All audit fixes applied (atomic flags, stack sizes, alignment, etc.)
 */

// =====================================================================
// INCLUDES
// =====================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "MPU9250.h"
#include "DFRobot_BloodOxygen_S.h"
#include "EMGFilters.h"
#include <Preferences.h>
#include <atomic>
#include <math.h>

// BLE Communication (Phase 5)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Edge AI model and TFLite runtime
#include "model_data.h"
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// =====================================================================
// PIN DEFINITIONS
// =====================================================================
#define EMG_PIN       34   // Analog input for OYMotion EMG sensor
#define VIB_PIN       25   // LRA vibration motor (LEDC PWM)
#define HR_INT_PIN    4    // DFRobot SEN0344 interrupt output (active-low)

#define I2C_SDA       21
#define I2C_SCL       22

// =====================================================================
// DISPLAY SETTINGS
// =====================================================================
#define I2C_ADDR_DISPLAY  0x3C
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define OLED_RESET        -1

// =====================================================================
// SENSOR I2C ADDRESSES
// =====================================================================
#define I2C_ADDR_MPU  0x68

// =====================================================================
// TASK CONFIGURATION (stack sizes per audit F-W2/W3)
// =====================================================================
#define EMG_TASK_STACK       4096   // High-freq 1kHz sampling needs room
#define SENSOR_TASK_STACK    4096
#define DISPLAY_TASK_STACK   3072
#define SERIAL_TASK_STACK    4096
#define INFERENCE_TASK_STACK 4096

// =====================================================================
// HAPTIC CONFIGURATION (non-blocking, audit F-C1/F-W8)
// =====================================================================
#define HAPTIC_DURATION_MS    500
#define HAPTIC_COOLDOWN_MS    5000
#define HAPTIC_PWM_DUTY       180

// =====================================================================
// ADAPTIVE THRESHOLD CONFIGURATION
// =====================================================================
#define ADAPTIVE_WINDOW_SIZE       200
#define ADAPTIVE_UPDATE_INTERVAL_S 30    // seconds between recalculations

// Trained on synthetic data (2000 samples, 2026-04-09)
// Source: python/train_autoencoder.py --data data/raw/synthetic_baseline.csv
// Model params: 395, TFLite size: 4.1KB
#define INITIAL_THRESHOLD  0.00308f
#define NUM_FEATURES       3
#define NUM_OUTPUTS        3

// Normalization constants from models/normalization.json
const float NORM_MEAN[NUM_FEATURES] = {71.803f, 0.304f, 1.004f};
const float NORM_STD[NUM_FEATURES]  = {5.023f, 0.101f, 0.153f};

// =====================================================================
// GLOBAL OBJECTS
// =====================================================================
const int pwmFreq = 5000, pwmResolution = 8;

DFRobot_BloodOxygen_S_I2C bloodOxygen(&Wire, 0x57);
MPU9250 mpu;
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
EMGFilters emgFilter;

// =====================================================================
// PHYSIO STATE & SYNCHRONIZATION
// =====================================================================
struct PhysioState {
    float heart_rate;        // Real BPM from DFRobot BloodOxygen_S library
    float spo2;              // SpO2 % from DFRobot BloodOxygen_S library
    float emg_envelope;      // Properly filtered EMG squared envelope from EMGFilters
    float motion_magnitude;  // Accel magnitude from MPU-9250
    unsigned long timestamp; // millis() at last sensorTask update (authoritative)
};

PhysioState gState;
SemaphoreHandle_t gStateMutex;

// =====================================================================
// EDGE AI / TFLITE CONFIGURATION
// =====================================================================
#define TENSOR_ARENA_SIZE 8*1024   // 8KB arena — plenty for 395 params

// TFLite Micro globals
tflite::MicroErrorReporter  gMicroErrorReporter;
tflite::ErrorReporter*      gErrorReporter = &gMicroErrorReporter;
const tflite::Model*        gTfModel = nullptr;
tflite::MicroInterpreter*   gInterpreter = nullptr;
TfLiteTensor*               gTfInput = nullptr;
TfLiteTensor*               gTfOutput = nullptr;

// F-W1: 16-byte alignment for TFLite arena
alignas(16) uint8_t gTensorArena[TENSOR_ARENA_SIZE];

bool gModelReady = false;

// =====================================================================
// ADAPTIVE THRESHOLD STATE
// =====================================================================
float gAdaptiveThreshold = INITIAL_THRESHOLD;
float gMseBuffer[ADAPTIVE_WINDOW_SIZE];
int gMseBufferIndex = 0;
bool gMseBufferFull = false;
unsigned long gLastThresholdUpdate = 0;

// =====================================================================
// CROSS-CORE FLAGS (atomic, audit F-C4/F-W7)
// =====================================================================
std::atomic<bool> gAnomalyDetected{false};
std::atomic<bool> deviceConnected{false};
std::atomic<bool> oldDeviceConnected{false};

// HR polling mode — no interrupt flag needed (sensorTask polls every 4s)

// =====================================================================
// EMG EMA smoothing
// =====================================================================
float gEmgEMA = 0.0f;  // Exponential moving average of EMG envelope
const float EMG_EMA_ALPHA = 0.01f;  // Smooth over ~100 samples at 1000Hz (~100ms)

// =====================================================================
// HAPTIC STATE (non-blocking, audit F-C1)
// =====================================================================
unsigned long gHapticStart = 0;
bool gHapticActive = false;
unsigned long gLastHapticTime = 0;

// =====================================================================
// BLE CONFIGURATION (Phase 5)
// =====================================================================
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TELEMETRY_CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define ALERT_CHAR_UUID        "1c95d6e4-5dc9-4659-9290-43283a3b8d5a"

BLEServer*          pServer        = nullptr;
BLECharacteristic*  pTelemetryChar = nullptr;
BLECharacteristic*  pAlertChar     = nullptr;

// BLE Server Callbacks — track connection state using atomics
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected.store(true);
    }
    void onDisconnect(BLEServer* pServer) override {
        deviceConnected.store(false);
        Serial.println("[BLE] Device disconnected");
    }
};

// BLE Characteristic Callbacks — detect notify failures via onStatus
// notify() returns void — the only way to detect failures is this callback.
class TelemetryCharCallbacks : public BLECharacteristicCallbacks {
    void onStatus(BLECharacteristic *pChar, Status s, uint32_t code) override {
        if (s == ERROR_GATT) {
            Serial.printf("[BLE] Telemetry notify FAILED: 0x%04x\n", code);
        } else if (s == ERROR_NOTIFY_DISABLED) {
            Serial.println("[BLE] Telemetry: client disabled notifications");
        }
    }
};

class AlertCharCallbacks : public BLECharacteristicCallbacks {
    void onStatus(BLECharacteristic *pChar, Status s, uint32_t code) override {
        if (s == ERROR_GATT) {
            Serial.printf("[BLE] Alert notify FAILED: 0x%04x\n", code);
        }
    }
};

// =====================================================================
// FUNCTION DECLARATIONS
// =====================================================================
void emgTask(void* pvParameters);
void sensorTask(void* pvParameters);
void displayTask(void* pvParameters);
void serialTask(void* pvParameters);
void inferenceTask(void* pvParameters);

// =====================================================================
// SETUP
// =====================================================================
void setup() {
    // F-W5: Serial timeout — don't block forever if USB not connected
    Serial.begin(115200);
    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < 3000) { ; }

    Serial.println("=== Senseware Phase 5 Boot ===");

    // --- I2C Bus ---
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("I2C bus initialized.");

    // --- MPU-9250 ---
    if (!mpu.setup(I2C_ADDR_MPU)) {
        Serial.println("MPU connection failed. Halting.");
        while (1) { delay(1000); }
    }
    Serial.println("MPU-9250 initialized.");

    // --- DFRobot BloodOxygen_S (SEN0344) ---
    // IMPORTANT: The library's begin() calls Wire.begin() internally with NO
    // arguments, which resets the I2C bus. On FireBeetle the default pins (21/22)
    // match our explicit pins, but the reset can corrupt in-flight state.
    // We re-initialize Wire AFTER the library's begin() to ensure clean state.
    Serial.println("Initializing BloodOxygen_S (SEN0344)...");
    if (!bloodOxygen.begin()) {
        Serial.println("BloodOxygen_S not found on I2C 0x57. Halting.");
        while (1) { delay(1000); }
    }
    // Re-init Wire to undo the library's begin() bus reset
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("BloodOxygen_S I2C probe OK. Re-initialized Wire bus.");

    // Start PPG collection — sensor MCU begins accumulating samples
    bloodOxygen.sensorStartCollect();
    Serial.println("BloodOxygen_S collecting. First valid reading in ~8 seconds.");

    // --- HR polling will be handled in sensorTask (no interrupt needed) ---
    Serial.println("HR: polling mode (4s interval, 8s warm-up)");

    // --- SH1106 OLED ---
    Serial.println("Initializing SH1106 OLED...");
    if (!display.begin(I2C_ADDR_DISPLAY, true)) {
        Serial.println("SH1106 allocation failed. Halting.");
        while (1) { delay(1000); }
    }
    display.clearDisplay();
    display.display();
    Serial.println("SH1106 OLED initialized.");

    // --- Vibration Motor (LEDC) ---
    ledcAttach(VIB_PIN, pwmFreq, pwmResolution);
    ledcWrite(VIB_PIN, 0);
    Serial.println("Vibration motor initialized.");

    // --- EMG ADC ---
    analogReadResolution(12); // 0-4095
    analogSetPinAttenuation(EMG_PIN, ADC_11db);
    Serial.println("EMG ADC initialized.");

    // --- Mutex ---
    gStateMutex = xSemaphoreCreateMutex();
    if (gStateMutex == NULL) {
        Serial.println("Failed to create mutex. Halting.");
        while (1) { delay(1000); }
    }

    // Initialize state to safe defaults
    gState.heart_rate       = 0.0f;
    gState.spo2             = 0.0f;
    gState.emg_envelope     = 0.0f;
    gState.motion_magnitude = 0.0f;
    gState.timestamp        = millis();

    // --- Load persisted adaptive threshold from NVS ---
    Preferences prefs;
    prefs.begin("senseware", true); // read-only
    float stored = prefs.getFloat("threshold", 0.0f);
    prefs.end();
    if (stored > 0 && stored < INITIAL_THRESHOLD * 100.0f) {
        gAdaptiveThreshold = stored;
        Serial.print("[NVS] Loaded adaptive threshold: ");
        Serial.println(gAdaptiveThreshold, 6);
    } else {
        if (stored > 0) {
            Serial.print("[NVS] Stored threshold invalid (");
            Serial.print(stored, 6);
            Serial.println("), resetting");
            Preferences prefsRW;
            prefsRW.begin("senseware", false);
            prefsRW.putFloat("threshold", INITIAL_THRESHOLD);
            prefsRW.end();
        }
        gAdaptiveThreshold = INITIAL_THRESHOLD;
        Serial.print("[NVS] Using initial threshold: ");
        Serial.println(INITIAL_THRESHOLD, 6);
    }

    // --- TFLite Model Init ---
    gTfModel = tflite::GetModel(senseware_model_tflite);

    if (gTfModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.print("Model schema version mismatch: ");
        Serial.print(gTfModel->version());
        Serial.print(" vs expected ");
        Serial.println(TFLITE_SCHEMA_VERSION);
        Serial.println("Continuing without ML inference.");
    } else {
        // Use AllOpsResolver — small overhead but guarantees all ops are available
        static tflite::AllOpsResolver resolver;

        // Build the interpreter (static so it persists beyond setup)
        static tflite::MicroInterpreter static_interpreter(
            gTfModel, resolver, gTensorArena, TENSOR_ARENA_SIZE
        );
        gInterpreter = &static_interpreter;

        if (gInterpreter->AllocateTensors() != kTfLiteOk) {
            Serial.println("AllocateTensors() failed. Continuing without ML.");
        } else {
            gTfInput  = gInterpreter->input(0);
            gTfOutput = gInterpreter->output(0);
            gModelReady = true;

            size_t arenaUsed = gInterpreter->arena_used_bytes();
            Serial.println("Model loaded successfully.");
            Serial.print("  Arena used: ");
            Serial.print(arenaUsed);
            Serial.print(" / ");
            Serial.print(TENSOR_ARENA_SIZE);
            Serial.println(" bytes");
        }
    }

    // --- FreeRTOS Tasks ---
    xTaskCreatePinnedToCore(
        emgTask,
        "EMG Task",
        EMG_TASK_STACK,
        NULL,
        3,  // highest priority — must maintain 1000Hz
        NULL,
        0   // Core 0 — dedicated to high-frequency analog polling
    );

    xTaskCreatePinnedToCore(
        sensorTask,
        "Sensor Task",
        SENSOR_TASK_STACK,
        NULL,
        2,
        NULL,
        1   // Core 1 — I2C sensor reads
    );

    xTaskCreatePinnedToCore(
        displayTask,
        "Display Task",
        DISPLAY_TASK_STACK,
        NULL,
        1,
        NULL,
        1   // Core 1
    );

    xTaskCreatePinnedToCore(
        serialTask,
        "Serial Task",
        SERIAL_TASK_STACK,
        NULL,
        1,
        NULL,
        1   // Core 1
    );

    xTaskCreatePinnedToCore(
        inferenceTask,
        "Inference Task",
        INFERENCE_TASK_STACK,
        NULL,
        1,
        NULL,
        1   // Core 1 — 0.5 Hz inference
    );

    // --- BLE GATT Server (Phase 5) ---
    Serial.setDebugOutput(true);  // Enable verbose BLE stack logging
    BLEDevice::init("Senseware");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pTelemetryChar = pService->createCharacteristic(
        TELEMETRY_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    pTelemetryChar->setCallbacks(new TelemetryCharCallbacks());
    pTelemetryChar->addDescriptor(new BLE2902());

    pAlertChar = pService->createCharacteristic(
        ALERT_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    pAlertChar->setCallbacks(new AlertCharCallbacks());
    pAlertChar->addDescriptor(new BLE2902());

    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("BLE server started. Device: Senseware");

    Serial.println("=== Senseware Phase 5 Ready ===");
}

// =====================================================================
// LOOP — nothing to do, FreeRTOS drives everything
// =====================================================================
void loop() {
    // Non-blocking haptic timer check (runs every loop iteration)
    // This ensures the haptic turns off exactly after HAPTIC_DURATION_MS
    if (gHapticActive && (millis() - gHapticStart >= HAPTIC_DURATION_MS)) {
        ledcWrite(VIB_PIN, 0);
        gHapticActive = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // Yield to RTOS tasks
}

// =====================================================================
// HR INTERRUPT HANDLER
// =====================================================================
// HR interrupt removed — using polling approach instead (see sensorTask)

// =====================================================================
// EMG TASK (Core 0) — Exactly 1000 Hz via vTaskDelayUntil
// The EMGFilters library REQUIRES exactly 1000Hz sampling (1ms period).
// =====================================================================
void emgTask(void* pvParameters) {
    // Initialize EMGFilters: 1000Hz sample rate, 50Hz notch, all filters on
    emgFilter.init(SAMPLE_FREQ_1000HZ, NOTCH_FREQ_50HZ, true, true, true);

    // Pre-settle the IIR filters: the HPF (20Hz) and LPF (150Hz) states start
    // at zero. Feeding a constant ~2048 (sensor rest voltage at VCC/2) into
    // zero-initialized IIR filters causes a startup transient lasting ~2 seconds
    // where the output is thousands instead of ~0. Without this warm-up,
    // the squared envelope gets ~70,000+ baked into the EMA and takes minutes
    // to decay. Feed 2000 samples at 1ms each (~2 seconds) to settle.
    for (int i = 0; i < 2000; i++) {
        emgFilter.update(analogRead(EMG_PIN));
        vTaskDelay(1);
    }
    gEmgEMA = 0.0f;  // Reset EMA after warm-up
    Serial.println("[EMG] Filter warm-up complete (2000 samples)");

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // Read raw ADC and apply EMGFilters bandpass + notch
        int raw = analogRead(EMG_PIN);
        int filtered = emgFilter.update(raw);

        // Rectified squared envelope
        float envelope = (float)(filtered * filtered);

        // EMA smoothing — reduces noise while preserving signal dynamics
        gEmgEMA = EMG_EMA_ALPHA * envelope + (1.0f - EMG_EMA_ALPHA) * gEmgEMA;

        // Write to shared state under mutex
        // F-C2: Do NOT write gState.timestamp — only sensorTask is authoritative
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            gState.emg_envelope = gEmgEMA;
            xSemaphoreGive(gStateMutex);
        }

        // Exactly 1ms period for EMGFilters (1000 Hz)
        // FreeRTOS tick is 1ms by default on ESP32
        vTaskDelayUntil(&xLastWakeTime, 1);
    }
}

// =====================================================================
// SENSOR TASK (Core 1) — I2C reads + HR polling
// This is the authoritative timestamp writer (F-C2).
//
// DFRobot SEN0344 BloodOxygen_S library investigation notes:
//   - sensorStartCollect() writes {0x00, 0x01} to register 0x20 (start PPG)
//   - getHeartbeatSPO2() reads 8 bytes from register 0x0C via raw I2C
//     rbuf[0]   = SPO2 (0 → invalid → library returns -1)
//     rbuf[1]   = SPO2 valid flag (UNUSED by library)
//     rbuf[2-5] = Heartbeat, big-endian uint32 (0 → invalid → -1)
//     rbuf[6]   = Heartbeat valid flag (UNUSED by library)
//     rbuf[7]   = unused
//   - Library begin() calls Wire.begin() with no args — resets I2C bus.
//     On FireBeetle, default pins (21/22) match ours, but the reset can
//     corrupt in-flight I2C state. We re-init Wire after begin().
//   - Sensor internal MCU needs ~4-8 seconds after sensorStartCollect()
//     before the FIRST valid reading. Subsequent readings update every ~4s.
//   - No INT pin usage needed — polling at 4s interval matches sensor rate.
//   - Library returns SPO2/Heartbeat = -1 when no finger or still measuring.
// =====================================================================
void sensorTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    unsigned long lastHRRead = 0;
    unsigned long taskStart = millis();
    const unsigned long HR_READ_INTERVAL_MS = 4000;   // sensor updates every ~4s
    const unsigned long HR_WARMUP_MS        = 8000;   // sensor needs 4-8s for first reading
    int hrReadCount = 0;
    int hrValidCount = 0;

    for (;;) {
        // --- HR + SpO2: poll at sensor update rate (every ~4 seconds) ---
        unsigned long now = millis();
        if (now - lastHRRead >= HR_READ_INTERVAL_MS) {
            lastHRRead = now;
            hrReadCount++;

            // Skip first readings during sensor warm-up — the sensor's onboard MCU
            // needs time to accumulate PPG samples before the first valid output.
            // Without this, the first 1-2 reads return all zeros (SPO2=0, HR=0).
            if (now - taskStart < HR_WARMUP_MS && hrReadCount <= 2) {
                Serial.print("[HR] Skipping read #");
                Serial.print(hrReadCount);
                Serial.println(" (sensor warm-up)");
            } else {
                // Call the library's read method
                bloodOxygen.getHeartbeatSPO2();

                int rawHR   = bloodOxygen._sHeartbeatSPO2.Heartbeat;
                int rawSPO2 = bloodOxygen._sHeartbeatSPO2.SPO2;

                // Also do a raw I2C probe to see exactly what register 0x0C contains
                // This is diagnostic — helps identify if the sensor is responding at all
                uint8_t rawBuf[8] = {0};
                Wire.beginTransmission(0x57);
                Wire.write(0x0C);
                int wireStatus = Wire.endTransmission(false);  // false = send restart
                int bytesRead = 0;
                if (wireStatus == 0) {
                    Wire.requestFrom(0x57, (uint8_t)8);
                    bytesRead = Wire.available();
                    for (int i = 0; i < 8 && Wire.available(); i++) {
                        rawBuf[i] = Wire.read();
                    }
                }

                // Log raw data for first 10 reads (diagnostic)
                if (hrReadCount <= 10) {
                    Serial.print("[HR] Read #");
                    Serial.print(hrReadCount);
                    Serial.print(" wireStatus=");
                    Serial.print(wireStatus);
                    Serial.print(" bytes=");
                    Serial.print(bytesRead);
                    Serial.print(" raw=[");
                    for (int i = 0; i < 8; i++) {
                        if (i > 0) Serial.print(" ");
                        Serial.print(rawBuf[i], HEX);
                    }
                    Serial.print("] libSPO2=");
                    Serial.print(rawSPO2);
                    Serial.print(" libHR=");
                    Serial.println(rawHR);
                }

                // Library returns -1 when no valid data (finger off / still measuring)
                // Preserve this: only accept positive values as valid
                float hr   = (rawHR   > 0) ? (float)rawHR   : 0.0f;
                float spo2 = (rawSPO2 > 0) ? (float)rawSPO2 : 0.0f;

                // Track first valid reading
                if (rawHR > 0 && rawSPO2 > 0) {
                    if (hrValidCount == 0) {
                        Serial.print("[HR] FIRST VALID READING at ");
                        Serial.print(now / 1000);
                        Serial.print("s: HR=");
                        Serial.print(rawHR);
                        Serial.print(" SpO2=");
                        Serial.println(rawSPO2);
                    }
                    hrValidCount++;
                }

                // Periodic health check every 20 reads (~80 seconds)
                if (hrReadCount > 0 && hrReadCount % 20 == 0) {
                    Serial.print("[HR] Health: ");
                    Serial.print(hrReadCount);
                    Serial.print(" reads, ");
                    Serial.print(hrValidCount);
                    Serial.print(" valid, wireStatus=");
                    Serial.println(wireStatus);
                }

                if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    gState.heart_rate = hr;
                    gState.spo2 = spo2;
                    gState.timestamp = millis();
                    xSemaphoreGive(gStateMutex);
                }
            }
        }

        // --- MPU-9250: motion magnitude (always read at 20 Hz) ---
        float motion = 0.0f;
        if (mpu.update()) {
            float ax = mpu.getAccX();
            float ay = mpu.getAccY();
            float az = mpu.getAccZ();
            motion = sqrtf(ax * ax + ay * ay + az * az);
        }

        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gState.motion_magnitude = motion;
            gState.timestamp = millis();
            xSemaphoreGive(gStateMutex);
        }

        // 20 Hz -> 50 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
    }
}

// =====================================================================
// DISPLAY TASK (Core 1) — 2 Hz OLED refresh
// =====================================================================
void displayTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    char buf[64];

    for (;;) {
        float hr   = 0.0f;
        float spo2 = 0.0f;
        float emg  = 0.0f;
        float mot  = 0.0f;

        // Snapshot under mutex — release before any display operations
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            hr   = gState.heart_rate;
            spo2 = gState.spo2;
            emg  = gState.emg_envelope;
            mot  = gState.motion_magnitude;
            xSemaphoreGive(gStateMutex);
        }

        bool alert = gAnomalyDetected.load();

        // Clear and redraw
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        // Title
        display.setCursor(0, 0);
        display.println("Senseware P5");
        display.drawLine(0, 10, 128, 10, SH110X_WHITE);

        // Heart rate + SpO2
        display.setCursor(0, 14);
        snprintf(buf, sizeof(buf), "HR: %.0f  SpO2: %.0f%%", hr, spo2);
        display.println(buf);

        // EMG envelope
        display.setCursor(0, 26);
        snprintf(buf, sizeof(buf), "EMG: %.0f", emg);
        display.println(buf);

        // Motion magnitude
        display.setCursor(0, 38);
        snprintf(buf, sizeof(buf), "MOT: %.2fg", mot);
        display.println(buf);

        // Adaptive threshold
        display.setCursor(0, 48);
        snprintf(buf, sizeof(buf), "TH: %.5f", gAdaptiveThreshold);
        display.println(buf);

        // Alert status
        if (alert) {
            // Flash: invert text on even cycles via millis
            bool flash = (millis() / 250) % 2 == 0;
            display.setTextSize(2);
            display.setTextColor(flash ? SH110X_BLACK : SH110X_WHITE);
            display.setCursor(0, 56);
            display.println("! ALERT !");
            display.setTextColor(SH110X_WHITE);
        } else {
            display.setTextSize(1);
            display.setCursor(0, 56);
            display.println("STATUS: OK");
        }

        display.display();

        // 2 Hz -> 500 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
    }
}

// =====================================================================
// INFERENCE TASK (Core 1) — 0.5 Hz autoencoder anomaly detection
// With adaptive threshold + non-blocking haptic (audit F-C1)
// =====================================================================
void inferenceTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    float input[NUM_FEATURES];

    for (;;) {
        // Skip inference if model not loaded
        if (!gModelReady) {
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
            continue;
        }

        float hr  = 0.0f;
        float emg = 0.0f;
        float mot = 0.0f;

        // Snapshot under mutex — release before inference
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            hr  = gState.heart_rate;
            emg = gState.emg_envelope;
            mot = gState.motion_magnitude;
            xSemaphoreGive(gStateMutex);
        }

        // Skip inference when sensor not attached (HR=0 means no valid reading)
        if (hr <= 0.0f) {
            gAnomalyDetected.store(false);
            // Skip adaptive threshold update when no valid data
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
            continue;
        }

        // Normalize inputs: z-score
        input[0] = (hr  - NORM_MEAN[0]) / NORM_STD[0];
        input[1] = (emg - NORM_MEAN[1]) / NORM_STD[1];
        input[2] = (mot - NORM_MEAN[2]) / NORM_STD[2];

        // Copy normalized inputs into TFLite input tensor
        for (int i = 0; i < NUM_FEATURES; i++) {
            gTfInput->data.f[i] = input[i];
        }

        // Run TFLite inference
        unsigned long t0 = micros();
        TfLiteStatus status = gInterpreter->Invoke();
        unsigned long dt = micros() - t0;

        if (status != kTfLiteOk) {
            Serial.print("[ML] Invoke failed: ");
            Serial.println(status);
        } else {
            // Compute MSE: mean((input - output)^2)
            float mse = 0.0f;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                float diff = input[i] - gTfOutput->data.f[i];
                mse += diff * diff;
            }
            mse /= NUM_OUTPUTS;

            // --- Adaptive threshold: feed MSE into ring buffer ---
            gMseBuffer[gMseBufferIndex] = mse;
            gMseBufferIndex = (gMseBufferIndex + 1) % ADAPTIVE_WINDOW_SIZE;
            if (gMseBufferIndex == 0) {
                gMseBufferFull = true;
            }

            // Recompute adaptive threshold every ADAPTIVE_UPDATE_INTERVAL_S seconds
            unsigned long now = millis();
            if (now - gLastThresholdUpdate > (unsigned long)ADAPTIVE_UPDATE_INTERVAL_S * 1000UL
                && gMseBufferFull && hr > 0.0f) {

                float sum = 0.0f, sumSq = 0.0f;
                for (int i = 0; i < ADAPTIVE_WINDOW_SIZE; i++) {
                    sum += gMseBuffer[i];
                    sumSq += gMseBuffer[i] * gMseBuffer[i];
                }
                float mean = sum / ADAPTIVE_WINDOW_SIZE;
                float variance = sumSq / ADAPTIVE_WINDOW_SIZE - mean * mean;
                float std = sqrtf(fmaxf(variance, 0.0f));
                gAdaptiveThreshold = mean + 3.0f * std;

                // Cap threshold to prevent runaway (e.g., from no-finger condition before fix)
                const float MAX_THRESHOLD = INITIAL_THRESHOLD * 100.0f;  // ~0.308
                if (gAdaptiveThreshold > MAX_THRESHOLD) {
                    gAdaptiveThreshold = MAX_THRESHOLD;
                }

                gLastThresholdUpdate = now;

                // Persist to NVS
                Preferences prefs;
                prefs.begin("senseware", false); // read-write
                prefs.putFloat("threshold", gAdaptiveThreshold);
                prefs.putFloat("mean", mean);
                prefs.putFloat("std", std);
                prefs.end();

                // Log after mutex released and prefs closed
                Serial.print("[ADAPT] Threshold updated: ");
                Serial.print(gAdaptiveThreshold, 6);
                Serial.print(" (mean=");
                Serial.print(mean, 6);
                Serial.print(", std=");
                Serial.print(std, 6);
                Serial.println(")");
            }

            // --- Anomaly detection + non-blocking haptic ---
            if (mse > gAdaptiveThreshold) {
                gAnomalyDetected.store(true);

                // Non-blocking haptic trigger with cooldown (F-C1)
                now = millis();
                if (!gHapticActive && (now - gLastHapticTime > HAPTIC_COOLDOWN_MS)) {
                    gHapticActive = true;
                    gHapticStart = now;
                    gLastHapticTime = now;
                    ledcWrite(VIB_PIN, HAPTIC_PWM_DUTY);
                }

                Serial.print("[ALERT] MSE=");
                Serial.print(mse, 4);
                Serial.print(" thresh=");
                Serial.println(gAdaptiveThreshold, 4);
            } else {
                gAnomalyDetected.store(false);
            }

            // Debug: periodic MSE logging
            Serial.print("[ML] MSE=");
            Serial.print(mse, 6);
            Serial.print(" thresh=");
            Serial.print(gAdaptiveThreshold, 6);
            Serial.print(" us=");
            Serial.println(dt);
        }

        // 0.5 Hz -> 2000 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
    }
}

// =====================================================================
// SERIAL TASK (Core 1) — 1 Hz CSV output (Phase 2)
// Format: millis,heart_rate,spo2,emg_envelope,motion_magnitude,alert
// =====================================================================
void serialTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Print CSV header once
    Serial.println("millis,heart_rate,spo2,emg_envelope,motion_magnitude,alert");

    for (;;) {
        float hr   = 0.0f;
        float spo2 = 0.0f;
        float emg  = 0.0f;
        float mot  = 0.0f;
        unsigned long ts = 0;
        bool alert = false;

        // Snapshot under mutex — release before Serial/BLE operations
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            hr   = gState.heart_rate;
            spo2 = gState.spo2;
            emg  = gState.emg_envelope;
            mot  = gState.motion_magnitude;
            ts   = gState.timestamp;
            xSemaphoreGive(gStateMutex);
        }

        alert = gAnomalyDetected.load();

        // CSV row
        Serial.print(ts);
        Serial.print(",");
        Serial.print(hr, 1);
        Serial.print(",");
        Serial.print(spo2, 1);
        Serial.print(",");
        Serial.print(emg, 2);
        Serial.print(",");
        Serial.print(mot, 2);
        Serial.print(",");
        Serial.println(alert ? 1 : 0);

        // BLE telemetry notifications
        if (deviceConnected.load()) {
            uint8_t telemetry[16];
            memcpy(telemetry,      &hr,   4);
            memcpy(telemetry + 4,  &spo2, 4);
            memcpy(telemetry + 8,  &emg,  4);
            memcpy(telemetry + 12, &mot,  4);
            pTelemetryChar->setValue(telemetry, 16);
            pTelemetryChar->notify();

            static int notifyCount = 0;
            if (++notifyCount % 10 == 0) {
                Serial.printf("[BLE] Notifications sent: %d\n", notifyCount);
            }

            uint8_t alertByte = alert ? 1 : 0;
            pAlertChar->setValue(&alertByte, 1);
            pAlertChar->notify();
        }

        // Handle BLE reconnection on disconnect (F-C4: atomic flags)
        if (!deviceConnected.load() && oldDeviceConnected.load()) {
            vTaskDelay(pdMS_TO_TICKS(500));  // small pause before restarting advertising
            pServer->startAdvertising();
            Serial.println("BLE: Restarting advertising");
        }
        oldDeviceConnected.store(deviceConnected.load());

        // 1 Hz -> 1000 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}
