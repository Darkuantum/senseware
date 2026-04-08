#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "MPU9250.h"
#include "MAX30105.h"
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
#define EMG_PIN 34
#define VIB_PIN 25

#define I2C_SDA 21
#define I2C_SCL 22

// =====================================================================
// DISPLAY SETTINGS
// =====================================================================
#define I2C_ADDR_DISPLAY 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// =====================================================================
// SENSOR I2C ADDRESSES
// =====================================================================
#define I2C_ADDR_MPU   0x68
#define I2C_ADDR_MAX   0x57

// =====================================================================
// TASK CONFIGURATION
// =====================================================================
#define EMG_TASK_STACK    2048
#define SENSOR_TASK_STACK 4096
#define DISPLAY_TASK_STACK 2048
#define SERIAL_TASK_STACK 2048

// Inference task runs every 2s, needs extra stack for TFLite
#define INFERENCE_TASK_STACK 4096

// EMG envelope detector smoothing factor
#define EMG_ALPHA 0.1f

// ADC midpoint for 12-bit (0-4095)
#define ADC_MIDPOINT 2048

// =====================================================================
// GLOBAL OBJECTS
// =====================================================================
const int pwmChannel = 0, pwmFreq = 5000, pwmResolution = 8;
MAX30105 particleSensor;
MPU9250 mpu;
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =====================================================================
// PHYSIO STATE & SYNCHRONIZATION
// =====================================================================
struct PhysioState {
    float heart_rate;        // BPM from MAX30102
    float emg_envelope;      // Smoothed EMG from GPIO 34
    float motion_magnitude;  // Accel magnitude from MPU-9250
    unsigned long timestamp; // millis() at sample time
};

volatile PhysioState gState;
SemaphoreHandle_t gStateMutex;

// Persistent EMG envelope across samples
float gEmgEnvelope = 0.0f;

// =====================================================================
// EDGE AI / TFLITE CONFIGURATION
// =====================================================================
#define TENSOR_ARENA_SIZE 8*1024   // 8KB arena — plenty for 395 params
#define NUM_FEATURES      3
#define NUM_OUTPUTS       3

// TFLite Micro globals
tflite::MicroErrorReporter  gMicroErrorReporter;
tflite::ErrorReporter*      gErrorReporter = &gMicroErrorReporter;
const tflite::Model*        gTfModel = nullptr;
tflite::MicroInterpreter*   gInterpreter = nullptr;
TfLiteTensor*               gTfInput = nullptr;
TfLiteTensor*               gTfOutput = nullptr;
uint8_t                     gTensorArena[TENSOR_ARENA_SIZE];
bool                        gModelReady = false;

// Normalization constants (from Phase 3 training)
const float NORM_MEAN[NUM_FEATURES] = {71.803f, 0.304f, 1.004f};
const float NORM_STD[NUM_FEATURES]  = {5.023f, 0.101f, 0.153f};
const float ANOMALY_THRESHOLD       = 0.00308f;

// Alert state — written by inferenceTask, read by display/serial tasks
volatile bool gAnomalyDetected = false;

// =====================================================================
// BLE CONFIGURATION (Phase 5)
// =====================================================================
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TELEMETRY_CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define ALERT_CHAR_UUID        "1c95d6e4-5dc9-4659-9290-43283a3b8d5a"

BLEServer*        pServer        = nullptr;
BLECharacteristic* pTelemetryChar = nullptr;
BLECharacteristic* pAlertChar     = nullptr;
bool              deviceConnected    = false;
bool              oldDeviceConnected = false;

// BLE Server Callbacks — track connection state
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
    }
    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
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
    Serial.begin(115200);
    while (!Serial) { ; } // Wait for serial (USB) — acceptable in setup only

    Serial.println("=== Senseware Phase 4 Boot ===");

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

    // --- MAX30102 ---
    Serial.println("Initializing MAX30102...");
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST, I2C_ADDR_MAX)) {
        Serial.println("MAX30102 not found. Halting.");
        while (1) { delay(1000); }
    }

    // MAX30102 configuration
    byte ledBrightness = 60;  // 0=Off to 255=50mA
    byte sampleAverage  = 4;  // 1, 2, 4, 8, 16, 32
    byte ledMode        = 2;  // 1=Red, 2=Red+IR, 3=Red+IR+Green
    int  sampleRate     = 100;
    int  pulseWidth     = 411;
    int  adcRange       = 4096;
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    Serial.println("MAX30102 initialized.");

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
    gState.emg_envelope     = 0.0f;
    gState.motion_magnitude = 0.0f;
    gState.timestamp        = millis();

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
        2,  // priority
        NULL,
        0   // Core 0 — high-frequency analog polling
    );

    xTaskCreatePinnedToCore(
        sensorTask,
        "Sensor Task",
        SENSOR_TASK_STACK,
        NULL,
        2,  // priority
        NULL,
        1   // Core 1 — I2C sensor reads
    );

    xTaskCreatePinnedToCore(
        displayTask,
        "Display Task",
        DISPLAY_TASK_STACK,
        NULL,
        1,  // lower priority
        NULL,
        1   // Core 1
    );

    xTaskCreatePinnedToCore(
        serialTask,
        "Serial Task",
        SERIAL_TASK_STACK,
        NULL,
        1,  // lower priority
        NULL,
        1   // Core 1
    );

    xTaskCreatePinnedToCore(
        inferenceTask,
        "Inference Task",
        INFERENCE_TASK_STACK,
        NULL,
        1,  // priority
        NULL,
        1   // Core 1 — 2 Hz inference
    );

    // --- BLE GATT Server (Phase 5) ---
    BLEDevice::init("Senseware");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pTelemetryChar = pService->createCharacteristic(
        TELEMETRY_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTelemetryChar->addDescriptor(new BLE2902());

    pAlertChar = pService->createCharacteristic(
        ALERT_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pAlertChar->addDescriptor(new BLE2902());

    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("BLE server started. Device: Senseware");

    Serial.println("Senseware Phase 5 Ready");
}

// =====================================================================
// LOOP — nothing to do, FreeRTOS drives everything
// =====================================================================
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// =====================================================================
// EMG TASK (Core 0) — ~200 Hz envelope detector
// =====================================================================
void emgTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // Read raw ADC
        int raw = analogRead(EMG_PIN);

        // Rectify around ADC midpoint
        float rectified = fabsf(raw - ADC_MIDPOINT);

        // Exponential moving average (envelope detector)
        gEmgEnvelope = EMG_ALPHA * rectified + (1.0f - EMG_ALPHA) * gEmgEnvelope;

        // Write to shared state under mutex
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gState.emg_envelope = gEmgEnvelope;
            gState.timestamp = millis();
            xSemaphoreGive(gStateMutex);
        }

        // ~200 Hz → 5 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5));
    }
}

// =====================================================================
// SENSOR TASK (Core 1) — 20 Hz I2C reads (MPU + MAX30102)
// =====================================================================
void sensorTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // --- MPU-9250: motion magnitude ---
        float motion = 0.0f;
        if (mpu.update()) {
            float ax = mpu.getAccX();
            float ay = mpu.getAccY();
            float az = mpu.getAccZ();
            motion = sqrtf(ax * ax + ay * ay + az * az);
        }

        // --- MAX30102: IR value (scaled for display) ---
        // We read IR and store it as a heartbeat proxy.
        // Full beat detection will be added in a later phase.
        float hrProxy = 0.0f;
        uint32_t ir = particleSensor.getIR();
        // Map IR to a rough 0-200 range for display purposes
        // (real HR detection via peak-finding can replace this)
        if (ir > 50000) {  // finger likely on sensor
            hrProxy = map(ir, 50000, 300000, 40, 180);
            hrProxy = constrain(hrProxy, 40.0f, 200.0f);
        }

        // --- Write to shared state under mutex ---
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gState.heart_rate = hrProxy;
            gState.motion_magnitude = motion;
            gState.timestamp = millis();
            xSemaphoreGive(gStateMutex);
        }

        // 20 Hz → 50 ms period
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
        float emg  = 0.0f;
        float mot  = 0.0f;

        // Snapshot under mutex
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            hr  = gState.heart_rate;
            emg = gState.emg_envelope;
            mot = gState.motion_magnitude;
            xSemaphoreGive(gStateMutex);
        }

        // Clear and redraw
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        // Title
        display.setCursor(0, 0);
        display.println("Senseware P5");
        display.drawLine(0, 10, 128, 10, SH110X_WHITE);

        // Heart rate
        display.setCursor(0, 16);
        snprintf(buf, sizeof(buf), "HR: %.0f BPM", hr);
        display.println(buf);

        // EMG envelope
        display.setCursor(0, 30);
        snprintf(buf, sizeof(buf), "EMG: %.2f", emg);
        display.println(buf);

        // Motion magnitude
        display.setCursor(0, 44);
        snprintf(buf, sizeof(buf), "MOT: %.2fg", mot);
        display.println(buf);

        // Alert status (Phase 4 — Edge AI)
        if (gAnomalyDetected) {
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

        // 2 Hz → 500 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
    }
}

// =====================================================================
// INFERENCE TASK (Core 1) — 0.5 Hz autoencoder anomaly detection
// =====================================================================
void inferenceTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    float input[NUM_FEATURES];
    float mse;

    for (;;) {
        // Skip inference if model not loaded
        if (!gModelReady) {
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
            continue;
        }

        float hr  = 0.0f;
        float emg = 0.0f;
        float mot = 0.0f;

        // Snapshot under mutex (same pattern as displayTask)
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            hr  = gState.heart_rate;
            emg = gState.emg_envelope;
            mot = gState.motion_magnitude;
            xSemaphoreGive(gStateMutex);
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
            mse = 0.0f;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                float diff = input[i] - gTfOutput->data.f[i];
                mse += diff * diff;
            }
            mse /= NUM_OUTPUTS;

            if (mse > ANOMALY_THRESHOLD) {
                gAnomalyDetected = true;

                // Fire haptic motor for 500ms
                ledcWrite(VIB_PIN, 180);
                vTaskDelay(pdMS_TO_TICKS(500));
                ledcWrite(VIB_PIN, 0);

                Serial.print("[ALERT] MSE=");
                Serial.println(mse, 4);
            } else {
                gAnomalyDetected = false;
            }

            // Debug: periodic MSE logging
            Serial.print("[ML] MSE=");
            Serial.print(mse, 6);
            Serial.print(" us=");
            Serial.println(dt);
        }

        // 0.5 Hz → 2000 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
    }
}

// =====================================================================
// SERIAL TASK (Core 1) — 1 Hz CSV output (Phase 2 ready)
// =====================================================================
void serialTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Print CSV header once
    Serial.println("millis,heart_rate,emg_envelope,motion_magnitude,alert");

    for (;;) {
        float hr   = 0.0f;
        float emg  = 0.0f;
        float mot  = 0.0f;
        unsigned long ts = 0;
        bool alert = false;

        // Snapshot under mutex
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            hr  = gState.heart_rate;
            emg = gState.emg_envelope;
            mot = gState.motion_magnitude;
            ts  = gState.timestamp;
            xSemaphoreGive(gStateMutex);
        }

        alert = gAnomalyDetected;

        // CSV row
        Serial.print(ts);
        Serial.print(",");
        Serial.print(hr, 1);
        Serial.print(",");
        Serial.print(emg, 2);
        Serial.print(",");
        Serial.print(mot, 2);
        Serial.print(",");
        Serial.println(alert ? 1 : 0);

        // BLE telemetry notifications (Phase 5)
        if (deviceConnected) {
            // Pack 3 floats (HR, EMG, Motion) as raw bytes — 12 bytes total
            uint8_t telemetry[12];
            memcpy(telemetry, &hr, 4);
            memcpy(telemetry + 4, &emg, 4);
            memcpy(telemetry + 8, &mot, 4);
            pTelemetryChar->setValue(telemetry, 12);
            pTelemetryChar->notify();

            // Alert: 1 byte — 0x01 if anomaly active, 0x00 otherwise
            uint8_t alertByte = alert ? 1 : 0;
            pAlertChar->setValue(&alertByte, 1);
            pAlertChar->notify();
        }

        // Handle BLE reconnection on disconnect
        if (!deviceConnected && oldDeviceConnected) {
            delay(500);  // small pause before restarting advertising
            pServer->startAdvertising();
            Serial.println("BLE: Restarting advertising");
        }
        oldDeviceConnected = deviceConnected;

        // 1 Hz → 1000 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}
