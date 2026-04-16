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
 *   - Merged demo behavior: windowed anomaly detection, double-pulse haptic,
 *     alert blink, dino animation, accuracy %, HR placeholder when no finger
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
// I2C BUS RECOVERY
// =====================================================================
// Unsticks a hung SH1106 by cycling the I2C peripheral and toggling SCL
static void recoverI2C() {
    Wire.end();
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    // Toggle SCL a few times manually to clear stuck slaves
    pinMode(I2C_SCL, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL, HIGH);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL, LOW);
        delayMicroseconds(5);
    }
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
}

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
// HAPTIC CONFIGURATION — double-pulse pattern (merged from demo)
// =====================================================================
#define HAPTIC_DUTY         110
#define HAPTIC_PULSE_MS     220
#define HAPTIC_GAP_MS       180
#define HAPTIC_COOLDOWN_MS  15000  // Matches anomaly cooldown

// =====================================================================
// ANOMALY DETECTION — confirmation count + windowing (from demo)
// =====================================================================
#define ANOMALY_CONFIRM_COUNT  3
#define ANOMALY_WINDOW_MS      5000
#define ANOMALY_COOLDOWN_MS    15000

// =====================================================================
// ALERT BLINK CONFIGURATION (from demo)
// =====================================================================
#define ALERT_BLINK_INTERVAL_MS  200
#define ALERT_BLINK_TOGGLES      6   // 3 blinks

// =====================================================================
// ADAPTIVE THRESHOLD CONFIGURATION
// =====================================================================
#define ADAPTIVE_WINDOW_SIZE       200
#define ADAPTIVE_UPDATE_INTERVAL_S 30    // seconds between recalculations

// Trained on real baseline data (2026-04-16)
// Source: python/train_autoencoder.py --data data/raw/
// Model params: 395, TFLite size: 4.1KB
#define INITIAL_THRESHOLD  0.092563f
#define NUM_FEATURES       3
#define NUM_OUTPUTS        3

// Normalization constants from models/normalization.json
// Mean: HR=71.6000, EMG=6.6239, MOT=1.0190
// Std:  HR=1.0000,  EMG=146.1824, MOT=0.0391
const float NORM_MEAN[NUM_FEATURES] = {71.6000f, 6.6239f, 1.0190f};
const float NORM_STD[NUM_FEATURES]  = {1.0000f,  146.1824f, 0.0391f};

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
SemaphoreHandle_t gSerialMutex;  // Protects serial output from interleaving across tasks

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
static int sEmgCalThreshold = 0;  // Max rest noise (ADC²), set during calibration

// =====================================================================
// EMG DIAGNOSTIC GLOBALS (written from emgTask, read by serial/inference)
// =====================================================================
volatile int gEmgRawADC = 0;       // Raw analogRead value (0-4095)
volatile int gEmgFiltered = 0;     // Output from emgFilter.update()

// =====================================================================
// ACCURACY PERCENTAGE + LAST MSE (from demo)
// =====================================================================
float gAccuracyPercent = 0.0f;
float gLastMse = 0.0f;            // Written by inferenceTask, read by displayTask

// =====================================================================
// ANOMALY WINDOW STATE (from demo — confirmation count + 5s window + 15s cooldown)
// =====================================================================
int gAnomalyCounter = 0;
bool gWindowActive = false;
unsigned long gWindowStartMs = 0;
int gAnomalyEventCount = 0;
bool gPrevImuAnomaly = false;
unsigned long gContinuousAnomalyStartMs = 0;
bool gCooldownActive = false;
unsigned long gCooldownStartMs = 0;

// =====================================================================
// HAPTIC STATE — double-pulse pattern (from demo)
// =====================================================================
bool gVibrationSequenceActive = false;
bool gVibrationMotorOn = false;
unsigned long gVibrationStepStart = 0;
int gVibrationPulsesRemaining = 0;

// =====================================================================
// ALERT BLINK STATE (from demo)
// =====================================================================
bool gAlertBlinkActive = false;
unsigned long gAlertBlinkStart = 0;

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
        if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            Serial.println("[BLE] Device disconnected");
            xSemaphoreGive(gSerialMutex);
        }
    }
};

// BLE Characteristic Callbacks — detect notify failures via onStatus
// notify() returns void — the only way to detect failures is this callback.
class TelemetryCharCallbacks : public BLECharacteristicCallbacks {
    void onStatus(BLECharacteristic *pChar, Status s, uint32_t code) override {
        if (s == ERROR_GATT) {
            if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                Serial.printf("[BLE] Telemetry notify FAILED: 0x%04x\n", code);
                xSemaphoreGive(gSerialMutex);
            }
        } else if (s == ERROR_NOTIFY_DISABLED) {
            if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                Serial.println("[BLE] Telemetry: client disabled notifications");
                xSemaphoreGive(gSerialMutex);
            }
        }
    }
};

class AlertCharCallbacks : public BLECharacteristicCallbacks {
    void onStatus(BLECharacteristic *pChar, Status s, uint32_t code) override {
        if (s == ERROR_GATT) {
            if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                Serial.printf("[BLE] Alert notify FAILED: 0x%04x\n", code);
                xSemaphoreGive(gSerialMutex);
            }
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

// Demo-merged functions
void startDoubleVibration();
void updateVibration();
bool shouldShowAlertText();
void drawDinoFrame(int x, int y, int frame);
void drawCooldownDinoScreen();
float computeAccuracyPercent(float mse);

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

    // --- Serial mutex (prevents interleaved output between tasks) ---
    gSerialMutex = xSemaphoreCreateMutex();
    if (gSerialMutex == NULL) {
        Serial.println("Failed to create serial mutex. Halting.");
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
            Serial.println("[NVS] Threshold reset (was invalid)");
            Preferences prefsRW;
            prefsRW.begin("senseware", false);
            prefsRW.putFloat("threshold", INITIAL_THRESHOLD);
            prefsRW.end();
        }
        gAdaptiveThreshold = INITIAL_THRESHOLD;
        Serial.printf("[NVS] Threshold: %.6f (initial)\n", INITIAL_THRESHOLD);
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
    {
        BLE2902* pDesc2902 = new BLE2902();
        pDesc2902->setNotifications(true);
        pTelemetryChar->addDescriptor(pDesc2902);
    }

    pAlertChar = pService->createCharacteristic(
        ALERT_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    pAlertChar->setCallbacks(new AlertCharCallbacks());
    {
        BLE2902* pDesc2902 = new BLE2902();
        pDesc2902->setNotifications(true);
        pAlertChar->addDescriptor(pDesc2902);
    }

    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("BLE server started. Device: Senseware");

    // --- Ensure clean startup state (from demo) ---
    gVibrationSequenceActive = false;
    gVibrationMotorOn = false;
    gAlertBlinkActive = false;
    gCooldownActive = false;
    gWindowActive = false;
    gAnomalyEventCount = 0;
    gPrevImuAnomaly = false;
    gContinuousAnomalyStartMs = 0;

    Serial.println("=== Senseware Phase 5 Ready ===");
}

// =====================================================================
// LOOP — non-blocking haptic update + yield to RTOS
// =====================================================================
void loop() {
    // Non-blocking double-pulse haptic timer (from demo)
    updateVibration();

    vTaskDelay(pdMS_TO_TICKS(10));  // Yield to RTOS tasks
}

// =====================================================================
// DOUBLE-PULSE HAPTIC (from demo)
// =====================================================================
void startDoubleVibration() {
    gVibrationSequenceActive = true;
    gVibrationMotorOn = true;
    gVibrationStepStart = millis();
    gVibrationPulsesRemaining = 2;

    ledcWrite(VIB_PIN, HAPTIC_DUTY * 0.8); // slightly softer start

    gAlertBlinkActive = true;
    gAlertBlinkStart = millis();
}

void updateVibration() {
    if (!gVibrationSequenceActive) return;

    unsigned long now = millis();

    if (gVibrationMotorOn) {
        if (now - gVibrationStepStart >= HAPTIC_PULSE_MS) {
            ledcWrite(VIB_PIN, 0);
            gVibrationMotorOn = false;
            gVibrationStepStart = now;
            gVibrationPulsesRemaining--;

            if (gVibrationPulsesRemaining <= 0) {
                gVibrationSequenceActive = false;
            }
        }
    } else {
        if (gVibrationPulsesRemaining > 0 && now - gVibrationStepStart >= HAPTIC_GAP_MS) {
            ledcWrite(VIB_PIN, HAPTIC_DUTY);
            gVibrationMotorOn = true;
            gVibrationStepStart = now;
        }
    }
}

// =====================================================================
// ALERT BLINK (from demo)
// =====================================================================
bool shouldShowAlertText() {
    if (!gAlertBlinkActive) return false;

    unsigned long elapsed = millis() - gAlertBlinkStart;
    unsigned long totalDuration = ALERT_BLINK_INTERVAL_MS * ALERT_BLINK_TOGGLES;

    if (elapsed >= totalDuration) {
        gAlertBlinkActive = false;
        return false;
    }

    unsigned long phase = elapsed / ALERT_BLINK_INTERVAL_MS;
    return (phase % 2 == 0);
}

// =====================================================================
// ACCURACY PERCENTAGE (from demo)
// =====================================================================
float computeAccuracyPercent(float mse) {
    if (mse < 0.0f) return 0.0f;
    float denom = max(gAdaptiveThreshold, 0.000001f);
    float acc = 100.0f * expf(-mse / denom);
    return constrain(acc, 0.0f, 100.0f);
}

// =====================================================================
// DINO ANIMATION (from demo — verbatim)
// =====================================================================
void drawDinoFrame(int x, int y, int frame) {
  display.fillRect(x + 10, y + 2, 14, 10, SH110X_WHITE);
  display.drawPixel(x + 20, y + 5, SH110X_BLACK);

  // smiling mouth
  display.drawPixel(x + 16, y + 9, SH110X_BLACK);
  display.drawPixel(x + 17, y + 10, SH110X_BLACK);
  display.drawPixel(x + 18, y + 10, SH110X_BLACK);
  display.drawPixel(x + 19, y + 9, SH110X_BLACK);

  display.fillRect(x + 8, y + 12, 18, 14, SH110X_WHITE);

  display.fillRect(x + 3, y + 16, 6, 4, SH110X_WHITE);
  display.fillRect(x + 0, y + 17, 3, 2, SH110X_WHITE);

  if (frame == 0) {
    display.fillRect(x + 24, y + 14, 5, 2, SH110X_WHITE);
    display.fillRect(x + 24, y + 18, 4, 2, SH110X_WHITE);
    display.fillRect(x + 11, y + 26, 3, 8, SH110X_WHITE);
    display.fillRect(x + 20, y + 24, 3, 10, SH110X_WHITE);
  } else {
    display.fillRect(x + 24, y + 13, 4, 2, SH110X_WHITE);
    display.fillRect(x + 24, y + 19, 5, 2, SH110X_WHITE);
    display.fillRect(x + 11, y + 24, 3, 10, SH110X_WHITE);
    display.fillRect(x + 20, y + 26, 3, 8, SH110X_WHITE);
  }

  display.fillRect(x + 9, y + 33, 6, 2, SH110X_WHITE);
  display.fillRect(x + 18, y + 33, 6, 2, SH110X_WHITE);
}

void drawCooldownDinoScreen() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  int frame = (millis() / 250) % 2;
  drawDinoFrame(42, 10, frame);

  display.setTextSize(1);
  display.setCursor(26, 54);
  display.print("Cooling down");
  display.display();
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
    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.println("[EMG] Filter warm-up complete (2000 samples)");
        xSemaphoreGive(gSerialMutex);
    }

    // Calibrate baseline noise: sample at rest for ~3 seconds, record max squared
    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.println("[EMG] Calibrating baseline... keep muscle relaxed");
        xSemaphoreGive(gSerialMutex);
    }
    int maxEnv = 0;
    for (int i = 0; i < 3000; i++) {
        int raw = analogRead(EMG_PIN);
        int filtered = emgFilter.update(raw);
        int sqVal = filtered * filtered;
        if (sqVal > maxEnv) maxEnv = sqVal;
        vTaskDelay(1);
    }
    sEmgCalThreshold = maxEnv;
    gEmgEMA = 0.0f;
    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.print("[EMG] Calibration complete. Threshold (ADC²): ");
        Serial.println(sEmgCalThreshold);
        xSemaphoreGive(gSerialMutex);
    }

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // Read raw ADC and apply EMGFilters bandpass + notch
        int raw = analogRead(EMG_PIN);
        int filtered = emgFilter.update(raw);

        // Diagnostic globals (from demo) — for serial output
        gEmgRawADC = raw;
        gEmgFiltered = filtered;

        // Rectified squared envelope (ADC² — per OYMotion EMGFilters convention)
        // Clamp to zero if below calibration threshold (baseline noise floor)
        int envelope = filtered * filtered;
        if (envelope <= sEmgCalThreshold) envelope = 0;

        // EMA smoothing — reduces noise while preserving signal dynamics
        gEmgEMA = EMG_EMA_ALPHA * (float)envelope + (1.0f - EMG_EMA_ALPHA) * gEmgEMA;

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
                if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    Serial.printf("[HR] Skip #%d (warm-up)\n", hrReadCount);
                    xSemaphoreGive(gSerialMutex);
                }
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

                // Log simplified status for first 10 reads
                if (hrReadCount <= 10) {
                    bool valid = (rawHR > 0 && rawSPO2 > 0);
                    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        Serial.printf("[HR] Read #%d: %s\n", hrReadCount,
                            valid ? "OK" : "no signal");
                        xSemaphoreGive(gSerialMutex);
                    }
                }

                // Library returns -1 when no valid data (finger off / still measuring)
                // Preserve this: only accept positive values as valid
                float hr   = (rawHR   > 0) ? (float)rawHR   : 0.0f;
                float spo2 = (rawSPO2 > 0) ? (float)rawSPO2 : 0.0f;

                // Track first valid reading
                if (rawHR > 0 && rawSPO2 > 0) {
                    if (hrValidCount == 0) {
                        if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                            Serial.print("[HR] FIRST VALID READING at ");
                            Serial.print(now / 1000);
                            Serial.print("s: HR=");
                            Serial.print(rawHR);
                            Serial.print(" SpO2=");
                            Serial.println(rawSPO2);
                            xSemaphoreGive(gSerialMutex);
                        }
                    }
                    hrValidCount++;
                }

                // Periodic health check every 20 reads (~80 seconds)
                if (hrReadCount > 0 && hrReadCount % 20 == 0) {
                    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        Serial.printf("[HR] Status: %d/%d valid readings\n", hrValidCount, hrReadCount);
                        xSemaphoreGive(gSerialMutex);
                    }
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
// Priority order: ALERT blink → Cooldown dino → Normal metrics
// =====================================================================
void displayTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    char buf[64];

    // I2C hang watchdog — if we haven't completed a cycle in >2 seconds,
    // the I2C bus is stuck. Reset it.
    static unsigned long lastDisplayCycle = millis();

    for (;;) {
        if (millis() - lastDisplayCycle > 2000) {
            recoverI2C();
            // Re-init the OLED after bus reset
            display.begin(0x3C, true);
            display.clearDisplay();
            display.display();
            Serial.println("[OLED] I2C bus recovered after hang");
        }
        lastDisplayCycle = millis();

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

        // --- Priority 1: Full-screen ALERT blink (from demo) ---
        if (shouldShowAlertText()) {
            display.clearDisplay();
            display.setTextColor(SH110X_WHITE);
            display.setTextSize(2);

            const char* msg = "ALERT!";
            int16_t x1, y1;
            uint16_t w, h;
            display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);

            int x = (128 - w) / 2;
            int y = (64 - h) / 2;

            display.setCursor(x, y);
            display.print(msg);
            display.display();
            lastDisplayCycle = millis();

            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
            continue;
        }

        // --- Priority 2: Cooldown dino animation (from demo) ---
        if (gCooldownActive) {
            drawCooldownDinoScreen();
            lastDisplayCycle = millis();

            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
            continue;
        }

        // --- Priority 3: Normal metrics ---
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        // Title
        display.setCursor(0, 0);
        display.println("Senseware P5");
        display.drawLine(0, 10, 128, 10, SH110X_WHITE);

        // Heart rate + SpO2 (display raw — shows 0 when no finger)
        display.setCursor(0, 14);
        snprintf(buf, sizeof(buf), "HR: %.0f  SpO2: %.0f%%", hr, spo2);
        display.println(buf);

        // EMG envelope
        display.setCursor(0, 24);
        snprintf(buf, sizeof(buf), "EMG: %.0f", emg);
        display.println(buf);

        // Accuracy percentage (from demo)
        display.setCursor(0, 32);
        snprintf(buf, sizeof(buf), "ACC: %.1f%%", gAccuracyPercent);
        display.println(buf);

        // MSE (from demo)
        display.setCursor(0, 40);
        snprintf(buf, sizeof(buf), "MSE: %.5f", gLastMse);
        display.println(buf);

        // Adaptive threshold
        display.setCursor(0, 48);
        snprintf(buf, sizeof(buf), "TH: %.5f", gAdaptiveThreshold);
        display.println(buf);

        // Motion magnitude
        display.setCursor(0, 56);
        snprintf(buf, sizeof(buf), "MOT: %.2fg", mot);
        display.println(buf);

        display.display();
        lastDisplayCycle = millis();

        // 2 Hz -> 500 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
    }
}

// =====================================================================
// INFERENCE TASK (Core 1) — 0.5 Hz autoencoder anomaly detection
// With adaptive threshold + windowed detection + double-pulse haptic
// =====================================================================
void inferenceTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    float input[NUM_FEATURES];

    for (;;) {
        // Cooldown expiration — must run every cycle regardless of model state
        if (gCooldownActive && millis() - gCooldownStartMs >= ANOMALY_COOLDOWN_MS) {
            gCooldownActive = false;
            gWindowActive = false;
            gAnomalyEventCount = 0;
            gPrevImuAnomaly = false;
            gContinuousAnomalyStartMs = 0;
            gAnomalyDetected.store(false);
        }

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

        // HR placeholder (from demo): when no finger detected (HR=0),
        // use NORM_MEAN[0] as placeholder so inference can still run
        // (EMG + motion detection still useful without HR sensor).
        // gState.heart_rate keeps 0 for display purposes.
        float heartRateForModel = (hr > 0.0f) ? hr : NORM_MEAN[0];

        // Normalize inputs: z-score
        input[0] = (heartRateForModel - NORM_MEAN[0]) / NORM_STD[0];
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
            if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                Serial.print("[ML] Invoke failed: ");
                Serial.println(status);
                xSemaphoreGive(gSerialMutex);
            }
            gLastMse = -1.0f;
            gAccuracyPercent = 0.0f;
        } else {
            // Compute MSE: mean((input - output)^2)
            float mse = 0.0f;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                float diff = input[i] - gTfOutput->data.f[i];
                mse += diff * diff;
            }
            mse /= NUM_OUTPUTS;

            gLastMse = mse;
            gAccuracyPercent = computeAccuracyPercent(mse);

            // --- Adaptive threshold: feed MSE into ring buffer ---
            gMseBuffer[gMseBufferIndex] = mse;
            gMseBufferIndex = (gMseBufferIndex + 1) % ADAPTIVE_WINDOW_SIZE;
            if (gMseBufferIndex == 0) {
                gMseBufferFull = true;
            }

            // Recompute adaptive threshold every ADAPTIVE_UPDATE_INTERVAL_S seconds
            unsigned long now = millis();
            if (now - gLastThresholdUpdate > (unsigned long)ADAPTIVE_UPDATE_INTERVAL_S * 1000UL
                && gMseBufferFull) {

                float sum = 0.0f, sumSq = 0.0f;
                for (int i = 0; i < ADAPTIVE_WINDOW_SIZE; i++) {
                    sum += gMseBuffer[i];
                    sumSq += gMseBuffer[i] * gMseBuffer[i];
                }
                float mean = sum / ADAPTIVE_WINDOW_SIZE;
                float variance = sumSq / ADAPTIVE_WINDOW_SIZE - mean * mean;
                float std = sqrtf(fmaxf(variance, 0.0f));
                float oldThreshold = gAdaptiveThreshold;
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
                if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    Serial.printf("[ADAPT] Threshold: %.6f (was %.6f)\n", gAdaptiveThreshold, oldThreshold);
                    xSemaphoreGive(gSerialMutex);
                }
            }

            // --- Anomaly detection: windowed multi-stage (from demo) ---
            bool anomaly = false;
            bool imuAnomalyNow = false;

            if (!gCooldownActive) {
                if (mse >= 0.0f && mse > gAdaptiveThreshold) {
                    gAnomalyCounter++;
                } else {
                    gAnomalyCounter = 0;
                }
                imuAnomalyNow = (gAnomalyCounter >= ANOMALY_CONFIRM_COUNT);
            } else {
                gAnomalyCounter = 0;
            }

            now = millis();

            if (!gCooldownActive) {
                // Start a new 5s window on first confirmed anomaly
                if (!gWindowActive && imuAnomalyNow) {
                    gWindowActive = true;
                    gWindowStartMs = now;
                    gAnomalyEventCount = 1;
                    gContinuousAnomalyStartMs = now;
                    gPrevImuAnomaly = true;
                }

                if (gWindowActive) {
                    if (imuAnomalyNow && !gPrevImuAnomaly) {
                        gAnomalyEventCount++;
                        gContinuousAnomalyStartMs = now;
                    }

                    if (!imuAnomalyNow) {
                        gContinuousAnomalyStartMs = 0;
                    }

                    bool continuousForWholeWindow =
                        imuAnomalyNow &&
                        gContinuousAnomalyStartMs > 0 &&
                        (now - gContinuousAnomalyStartMs >= ANOMALY_WINDOW_MS);

                    bool windowExpired = (now - gWindowStartMs >= ANOMALY_WINDOW_MS);

                    if (continuousForWholeWindow || (windowExpired && gAnomalyEventCount >= 3)) {
                        // TRIGGER ALERT
                        anomaly = true;
                        gAnomalyDetected.store(true);

                        startDoubleVibration();

                        gCooldownActive = true;
                        gCooldownStartMs = now;

                        gWindowActive = false;
                        gAnomalyEventCount = 0;
                        gPrevImuAnomaly = false;
                        gContinuousAnomalyStartMs = 0;

                        if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                            Serial.printf("[ALERT] Anomaly detected! MSE=%.3f  EMG=%.0f\n", mse, emg);
                            xSemaphoreGive(gSerialMutex);
                        }
                    } else if (windowExpired) {
                        gWindowActive = false;
                        gAnomalyEventCount = 0;
                        gPrevImuAnomaly = false;
                        gContinuousAnomalyStartMs = 0;
                        gAnomalyDetected.store(false);
                    } else {
                        anomaly = imuAnomalyNow;
                        gAnomalyDetected.store(anomaly);
                        gPrevImuAnomaly = imuAnomalyNow;
                    }
                } else {
                    anomaly = imuAnomalyNow;
                    gAnomalyDetected.store(anomaly);
                    gPrevImuAnomaly = imuAnomalyNow;
                }
            }

            // Clean aligned diagnostic line
            if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                Serial.printf("[DIAG] HR=%-3d  EMG=%-8.0f", (int)hr, emg);
                if (gEmgRawADC > 0) {
                    Serial.printf("(FLT=%-4d,RAW=%-4d)", gEmgFiltered, gEmgRawADC);
                }
                Serial.printf("  MOT=%-6.2f  MSE=%.6f  THR=%.6f  ACC=%5.1f%%  CNT=%d  ANOM=%s\n",
                    mot, mse, gAdaptiveThreshold, gAccuracyPercent, gAnomalyCounter,
                    anomaly ? "YES" : "NO");
                xSemaphoreGive(gSerialMutex);
            }
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
    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.println("millis,heart_rate,spo2,emg_envelope,motion_magnitude,alert,emg_cal_threshold");
        xSemaphoreGive(gSerialMutex);
    }

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
        if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            Serial.printf("[CSV] %lu,%.1f,%.1f,%.2f,%.2f,%d\n",
                ts, hr, spo2, emg, mot, alert ? 1 : 0);
            xSemaphoreGive(gSerialMutex);
        }

        // BLE telemetry notifications
        if (deviceConnected.load()) {
            uint8_t telemetry[16];
            memcpy(telemetry,      &hr,   4);
            memcpy(telemetry + 4,  &spo2, 4);
            memcpy(telemetry + 8,  &emg,  4);
            memcpy(telemetry + 12, &mot,  4);
            pTelemetryChar->setValue(telemetry, 16);
            pTelemetryChar->notify();
            vTaskDelay(pdMS_TO_TICKS(20));  // Let the TX buffer flush before next notify

            static int notifyCount = 0;
            if (++notifyCount % 10 == 0) {
                if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    Serial.printf("[BLE] Notifications sent: %d\n", notifyCount);
                    xSemaphoreGive(gSerialMutex);
                }
            }

            uint8_t alertByte = alert ? 1 : 0;
            pAlertChar->setValue(&alertByte, 1);
            pAlertChar->notify();
        }

        // Handle BLE reconnection on disconnect (F-C4: atomic flags)
        if (!deviceConnected.load() && oldDeviceConnected.load()) {
            vTaskDelay(pdMS_TO_TICKS(500));  // small pause before restarting advertising
            pServer->startAdvertising();
            if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                Serial.println("BLE: Restarting advertising");
                xSemaphoreGive(gSerialMutex);
            }
        }
        oldDeviceConnected.store(deviceConnected.load());

        // 1 Hz -> 1000 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}
