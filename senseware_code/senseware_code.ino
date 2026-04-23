/*
 * Senseware Firmware — Phase 5 (MAJOR REWRITE)
 * Target: ESP32-WROOM-32 (YD-ESP32 Type-A, FQBN esp32:esp32:esp32)
 *
 * Sensors:
 *   MAX30102 PPG (HR + SpO2) via SparkFun MAX30105.h + heartRate.h
 *   MPU-9250 9-DOF IMU
 *   OYMotion Analog EMG with EMGFilters (1000 Hz bandpass)
 *   SH1106 OLED 128x64
 *   LRA Vibration Motor via LEDC PWM
 *
 * Edge AI: TFLite Micro autoencoder for anomaly detection
 * WiFi: HTTP + SSE server for remote telemetry
 *
 * Major changes from prior version:
 *   - Replaced DFRobot SEN0344/DFRobot_BloodOxygen_S with SparkFun MAX30105 (direct MAX30102 register access)
 *   - Replaced naive EMG rectifier with EMGFilters (proper 20-150Hz bandpass)
 *   - EMG sampling at exactly 1000Hz via vTaskDelayUntil
 *   - HR reads via MAX30102 at 25Hz polling (SparkFun heartRate beat detection)
 *   - Adaptive threshold with NVS persistence (Preferences)
 *   - Non-blocking haptic with cooldown
 *   - All audit fixes applied (atomic flags, stack sizes, alignment, etc.)
 *   - Merged demo behavior: windowed anomaly detection, double-pulse haptic,
 *     alert blink, dino animation, accuracy %
 *   - WiFi + SSE server for remote telemetry
 *
 * Porting notes (core 3.x → 2.x):
 *   - LEDC: ledcAttach(pin, freq, res) → ledcSetup(ch, freq, res) + ledcAttachPin(pin, ch)
 *   - ledcWrite(pin, val) → ledcWrite(channel, val)
 *   - Sensor fallback: dead I2C sensors feed NORM_MEAN calibration values (z-score=0)
 *     to prevent false anomaly triggers
 */

// =====================================================================
// INCLUDES
// =====================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "MPU9250.h"
#include "MAX30105.h"
#include "heartRate.h"
#include "EMGFilters.h"
#include <Preferences.h>
#include <atomic>
#include <math.h>

// WiFi + HTTP + SSE server
#include <WiFi.h>

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


#define I2C_SDA       21
#define I2C_SCL       22

// =====================================================================
// I2C BUS RECOVERY
// =====================================================================
// Unsticks a hung SH1106 by cycling the I2C peripheral and toggling SCL
static void recoverI2C() {
    Wire.end();
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
    // Toggle SCL a few times manually to clear stuck slaves
    pinMode(I2C_SCL, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL, HIGH);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL, LOW);
        delayMicroseconds(5);
    }
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
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
#define HTTP_TASK_STACK      4096

// =====================================================================
// HAPTIC CONFIGURATION — double-pulse pattern (merged from demo)
// =====================================================================
#define HAPTIC_DUTY         255
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
const int pwmChannel = 0;

MAX30105 particleSensor;
MPU9250 mpu;
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
EMGFilters emgFilter;

// =====================================================================
// SENSOR HEALTH FLAGS
// =====================================================================
// When a sensor fails init, its flag is set false. The inference task
// substitutes NORM_MEAN for dead sensors (z-score=0) so they don't
// contribute to anomaly detection and can't trigger false alerts.
bool gHrSensorAlive = true;
bool gMpuAlive = true;

// =====================================================================
// PHYSIO STATE & SYNCHRONIZATION
// =====================================================================
struct PhysioState {
    float heart_rate;        // BPM from SparkFun heartRate.h (checkForBeat)
    float spo2;              // SpO2 % from MAX30102 red/IR ratio (AN6407 formula)
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
std::atomic<bool> gAnomalyDetected{false};

// HR: SparkFun heartRate checkForBeat() at 25Hz polling

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
// HR / SpO2 TRACKING (SparkFun MAX30105)
// =====================================================================

// heartRate.h file-scope globals — exposed for beat-detector state reset
// when IR drops to near-zero (finger removed). See heartRate.cpp lines 60-74.
extern int16_t IR_AC_Max, IR_AC_Min;
extern int16_t IR_AC_Signal_Current, IR_AC_Signal_Previous;
extern int16_t IR_AC_Signal_min, IR_AC_Signal_max;
extern int16_t IR_Average_Estimated;
extern int16_t positiveEdge, negativeEdge;
extern int32_t ir_avg_reg;
extern uint8_t offset;
extern int16_t cbuf[32];

long gLastBeatMs = 0;
float gHrBuffer[4] = {0};
int gHrBufIdx = 0;
float gSpo2EMA = 98.0f;
float gRedDC = 0.0f;
float gIrDC = 0.0f;
const float SPO2_EMA_ALPHA = 0.05f;
const float DC_EMA_ALPHA = 0.005f;

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
volatile bool gCooldownActive = false;
volatile unsigned long gCooldownStartMs = 0;

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
volatile bool gAlertBlinkActive = false;
volatile unsigned long gAlertBlinkStart = 0;

// =====================================================================
// WIFI + HTTP + SSE SERVER
// =====================================================================

// =====================================================================
// WIFI CONFIGURATION
// =====================================================================
// Set to 1 for AP mode (ESP32 creates its own WiFi), 0 for STA mode (joins existing WiFi)
#define SENSEWARE_WIFI_AP  0

#if SENSEWARE_WIFI_AP
  // AP mode — ESP32 creates its own WiFi network
  const char* AP_SSID     = "Senseware";
  const char* AP_PASSWORD = "senseware";  // min 8 chars for WPA2
#else
  // STA mode — ESP32 joins existing WiFi (phone hotspot, router, etc.)
  const char* WIFI_SSID     = "YOGA-PRO-DRK 5936";
  const char* WIFI_PASSWORD = "89Ww233=";
#endif

WiFiServer httpServer(81);

// SSE (Server-Sent Events) — single persistent connection for telemetry push
WiFiClient sseClient;
bool sseConnected = false;
unsigned long sseLastPush = 0;
const unsigned long SSE_PUSH_INTERVAL_MS = 1000;  // Push every 1 second
const unsigned long SSE_KEEPALIVE_MS = 3000;       // Send keepalive comment every 3s
unsigned long sseLastKeepalive = 0;

bool initWiFi() {
#if SENSEWARE_WIFI_AP
    Serial.println("[WIFI] Starting Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    delay(100);
    Serial.printf("[WIFI] AP started — SSID: %s\n", AP_SSID);
    Serial.printf("[WIFI] Connect your laptop/phone to '%s' (password: %s)\n", AP_SSID, AP_PASSWORD);
    Serial.printf("[WIFI] Dashboard: http://%s:81/\n", WiFi.softAPIP().toString().c_str());
    return true;
#else
    Serial.printf("[WIFI] Connecting to %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    
    Serial.println("\n[WIFI] Connection failed.");
    return false;
#endif
}

// Start HTTP + SSE server on port 81
void startHttpServer() {
    httpServer.begin();
#if SENSEWARE_WIFI_AP
    String ip = WiFi.softAPIP().toString();
#else
    String ip = WiFi.localIP().toString();
#endif
    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.printf("[HTTP] Server started — http://%s:81/events\n", ip.c_str());
        xSemaphoreGive(gSerialMutex);
    }
}

// HTTP request counter (for debug)
static int sHttpRequestCount = 0;

// =====================================================================
// HTTP/SSE TASK — dedicated FreeRTOS task for HTTP server + SSE push
// Runs on Core 1 with diagnostic logging for push-level debugging.
// =====================================================================
void httpTask(void* pvParameters) {
    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.println("[HTTP] Task started on core " + String(xPortGetCoreID()));
        xSemaphoreGive(gSerialMutex);
    }

    static int ssePushCount = 0;  // diagnostic: how many SSE pushes sent

    for (;;) {
        WiFiClient client = httpServer.available();

        if (!client) {
            // No new client — handle SSE push on existing connection
            if (sseConnected && millis() - sseLastPush >= SSE_PUSH_INTERVAL_MS) {
                if (sseClient && sseClient.connected()) {
                    char buf[280];
                    snprintf(buf, sizeof(buf),
                        "data: {\"hr\":%.1f,\"spo2\":%.1f,\"emg\":%.2f,\"mot\":%.3f,\"mse\":%.6f,\"acc\":%.1f,\"anomaly\":%d}\n\n",
                        gState.heart_rate, gState.spo2, gState.emg_envelope,
                        gState.motion_magnitude, gLastMse, gAccuracyPercent,
                        gAnomalyDetected.load() ? 1 : 0);

                    size_t written = sseClient.print(buf);
                    ssePushCount++;

                    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                        Serial.printf("[SSE] Push #%d (%d bytes) client=%s\n",
                            ssePushCount, written,
                            sseClient.connected() ? "OK" : "DEAD");
                        xSemaphoreGive(gSerialMutex);
                    }
                } else {
                    // Client went away
                    sseConnected = false;
                    ssePushCount = 0;
                    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                        Serial.println("[SSE] Client lost (connected()=false)");
                        xSemaphoreGive(gSerialMutex);
                    }
                }
                sseLastPush = millis();
            }

            // SSE keepalive comment (SSE spec: lines starting with : are ignored)
            // Only fires if no data push has happened within the keepalive window
            if (sseConnected && millis() - sseLastKeepalive >= SSE_KEEPALIVE_MS) {
                if (sseClient && sseClient.connected()) {
                    sseClient.println(": keepalive");
                    sseClient.flush();
                }
                sseLastKeepalive = millis();
            }

            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Guard: skip if this is our existing SSE client being returned by available()
        if (sseClient && sseConnected) {
            if (client.remoteIP() == sseClient.remoteIP() &&
                client.remotePort() == sseClient.remotePort()) {
                while (client.available()) client.read();
                continue;
            }
        }

        sHttpRequestCount++;
        client.setTimeout(2000);

        // Read request headers
        unsigned long start = millis();
        String requestLine = "";
        while (millis() - start < 500) {
            if (client.available()) {
                char c = client.read();
                requestLine += c;
                if (requestLine.endsWith("\r\n\r\n") || requestLine.endsWith("\n\n")) break;
                if (requestLine.length() > 512) break;
            }
        }

        int firstLineEnd = requestLine.indexOf('\n');
        String firstLine = (firstLineEnd > 0) ? requestLine.substring(0, firstLineEnd) : requestLine;
        firstLine.trim();

        if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            Serial.printf("[HTTP] #%d from %s:%d — %s\n",
                sHttpRequestCount,
                client.remoteIP().toString().c_str(),
                client.remotePort(),
                firstLine.c_str());
            xSemaphoreGive(gSerialMutex);
        }

        // Route: /events (SSE)
        if (requestLine.indexOf("GET /events") != -1) {
            if (sseClient && sseClient.connected()) {
                sseClient.stop();
            }
            ssePushCount = 0;

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/event-stream");
            client.println("Cache-Control: no-cache");
            client.println("Connection: keep-alive");
            client.println("Access-Control-Allow-Origin: *");
            client.println();
            client.flush();

            sseClient = client;
            sseConnected = true;
            sseLastPush = millis();
            sseLastKeepalive = millis();

            if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                Serial.printf("[SSE] Client connected from %s:%d\n",
                    client.remoteIP().toString().c_str(), client.remotePort());
                xSemaphoreGive(gSerialMutex);
            }
            continue;  // DON'T stop the client — persistent SSE
        }

        // Route: /telemetry (single-shot JSON)
        if (requestLine.indexOf("GET /telemetry") != -1) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "{\"hr\":%.1f,\"spo2\":%.1f,\"emg\":%.2f,\"mot\":%.3f,\"mse\":%.6f,\"acc\":%.1f,\"anomaly\":%d}",
                gState.heart_rate, gState.spo2, gState.emg_envelope,
                gState.motion_magnitude, gLastMse, gAccuracyPercent,
                gAnomalyDetected.load() ? 1 : 0);
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println("Connection: close");
            client.print("Content-Length: ");
            client.println(strlen(buf));
            client.println();
            client.print(buf);
            client.flush();
        }
        // Route: / (status page)
        else if (requestLine.indexOf("GET / ") != -1 || requestLine.indexOf("GET /\r\n") != -1 || requestLine.indexOf("GET /\n") != -1) {
            String ipStr;
            #if SENSEWARE_WIFI_AP
                ipStr = WiFi.softAPIP().toString();
            #else
                ipStr = WiFi.localIP().toString();
            #endif
            String body = "<html><body><h1>Senseware</h1>"
                          "<p>SSE: <a href='/events'>/events</a></p>"
                          "<p>JSON: <a href='/telemetry'>/telemetry</a></p>"
                          "<p>IP: " + ipStr + ":81</p>"
                          "</body></html>";
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println("Access-Control-Allow-Origin: *");
            client.println("Connection: close");
            client.print("Content-Length: ");
            client.println(body.length());
            client.println();
            client.print(body);
            client.flush();
        }
        // OPTIONS
        else if (requestLine.indexOf("OPTIONS") != -1) {
            client.println("HTTP/1.1 204 No Content");
            client.println("Access-Control-Allow-Origin: *");
            client.println("Access-Control-Allow-Methods: GET, OPTIONS");
            client.println("Access-Control-Allow-Headers: *");
            client.println("Connection: close");
            client.println();
            client.flush();
        }
        // 404
        else {
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.print("Not Found");
            client.flush();
        }

        client.stop();
    }
}

// =====================================================================
// FUNCTION DECLARATIONS
// =====================================================================
void emgTask(void* pvParameters);
void sensorTask(void* pvParameters);
void displayTask(void* pvParameters);
void serialTask(void* pvParameters);
void inferenceTask(void* pvParameters);
void httpTask(void* pvParameters);

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

    Serial.println("=== Senseware Boot (WiFi+HTTP) ===");

    // --- I2C Bus ---
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);  // 100kHz — more reliable with multiple devices on bus
    Serial.println("I2C bus initialized (100kHz).");

    // --- SparkFun MAX30105 (HR + SpO2) ---
    delay(500);
    Serial.println("Initializing MAX30105 (HR/SpO2)...");
    if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
        Serial.println("MAX30105 not found on I2C — continuing without HR/SpO2.");
        gHrSensorAlive = false;
    } else {
        Serial.println("MAX30105 found.");
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);
        particleSensor.setup(0x1F, 4, 3, 400, 411, 16384);
        particleSensor.setPulseAmplitudeRed(0xA0);
        particleSensor.setPulseAmplitudeIR(0xA0);
        Serial.println("MAX30105 LEDs on (Red=0xA0, IR=0xA0, ADC=16384). Place finger to start.");
    }

    // --- MPU-9250 ---
    delay(200);  // Settle I2C bus before adding more devices
    Wire.setClock(100000);
    mpu.verbose(true);
    if (!mpu.setup(I2C_ADDR_MPU)) {
        Serial.println("MPU connection FAILED — continuing without IMU.");
        gMpuAlive = false;
    } else {
        Serial.println("MPU-9250 initialized.");
    }

    // --- SH1106 OLED ---
    delay(100);
    Wire.setClock(100000);
    Serial.println("Initializing SH1106 OLED...");
    if (!display.begin(I2C_ADDR_DISPLAY, true)) {
        Serial.println("SH1106 allocation failed. Halting.");
        while (1) { delay(1000); }
    }
    display.clearDisplay();
    display.display();
    Serial.println("SH1106 OLED initialized.");

    // --- Vibration Motor (LEDC) — core 2.x API ---
    ledcSetup(pwmChannel, pwmFreq, pwmResolution);
    ledcAttachPin(VIB_PIN, pwmChannel);
    ledcWrite(pwmChannel, 0);
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

    // --- WiFi + HTTP server (non-blocking — continues without WiFi on failure) ---
    if (initWiFi()) {
        startHttpServer();
        // Show IP on OLED so user knows where to connect
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.setCursor(0, 0);
        display.print("WiFi OK");
        display.setCursor(0, 10);
#if SENSEWARE_WIFI_AP
        display.print(WiFi.softAPIP());
#else
        display.print(WiFi.localIP());
#endif
        display.setCursor(0, 20);
        display.print("/events");
        display.display();
        delay(2000);  // Show IP for 2 seconds before normal display takes over

        // HTTP/SSE server task on Core 1
        xTaskCreatePinnedToCore(
            httpTask,
            "HTTP Task",
            HTTP_TASK_STACK,
            NULL,
            2,   // priority 2 — higher than display/serial, lower than EMG
            NULL,
            1    // Core 1
        );
    }

    // --- Ensure clean startup state (from demo) ---
    gVibrationSequenceActive = false;
    gVibrationMotorOn = false;
    gAlertBlinkActive = false;
    gCooldownActive = false;
    gWindowActive = false;
    gAnomalyEventCount = 0;
    gPrevImuAnomaly = false;
    gContinuousAnomalyStartMs = 0;

    Serial.println("=== Senseware Ready ===");
}

// =====================================================================
// LOOP — non-blocking haptic update + yield to RTOS
// =====================================================================
void loop() {
    // Non-blocking double-pulse haptic timer (from demo)
    updateVibration();

    vTaskDelay(pdMS_TO_TICKS(10));
}

// =====================================================================
// DOUBLE-PULSE HAPTIC (from demo)
// =====================================================================
void startDoubleVibration() {
    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.printf("[HAPTIC] Triggered at %lu ms\n", millis());
        xSemaphoreGive(gSerialMutex);
    }

    gVibrationSequenceActive = true;
    gVibrationMotorOn = true;
    gVibrationStepStart = millis();
    gVibrationPulsesRemaining = 2;

    ledcWrite(pwmChannel, HAPTIC_DUTY);

    gAlertBlinkActive = true;
    gAlertBlinkStart = millis();
}

void updateVibration() {
    if (!gVibrationSequenceActive) return;

    unsigned long now = millis();

    if (gVibrationMotorOn) {
        if (now - gVibrationStepStart >= HAPTIC_PULSE_MS) {
            ledcWrite(pwmChannel, 0);
            gVibrationMotorOn = false;
            gVibrationStepStart = now;
            gVibrationPulsesRemaining--;

            if (gVibrationPulsesRemaining <= 0) {
                gVibrationSequenceActive = false;
            }
        }
    } else {
        if (gVibrationPulsesRemaining > 0 && now - gVibrationStepStart >= HAPTIC_GAP_MS) {
            ledcWrite(pwmChannel, HAPTIC_DUTY);
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
// MAX30102/SparkFun MAX30105 library notes:
//   - ADC range 16384 (18-bit, max ~262143) for finger-present thresholds
//   - IR LED at 0xFF for beat detection via checkForBeat() on IR channel
//   - Red LED at 0xFF for SpO2 from red/IR DC ratio (Maxim AN6407)
//   - Polling at 25Hz via particleSensor.getIR() + particleSensor.getRed()
//   - No INT pin needed — beat detection is purely software
// =====================================================================
void sensorTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    static int mpuCycle = 0;

    // Wait for MAX30105 to stabilize after LED init
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.println("[SENSOR] Task started — reading at 25Hz");
        xSemaphoreGive(gSerialMutex);
    }

    for (;;) {
        unsigned long now = millis();

        // --- MAX30105: read IR and Red at 25Hz ---
        long irValue = 0, redValue = 0;
        if (gHrSensorAlive) {
            irValue = particleSensor.getIR();
            redValue = particleSensor.getRed();
        }

        // --- Beat detection on IR channel (SparkFun heartRate library) ---
        // heartRate.h has 13 file-scope globals (DC estimator, FIR buffer,
        // zero-crossing state) that get poisoned when IR drops to near-zero.
        // Reset them when no finger is present so the detector is clean
        // when the finger returns.
        if (gHrSensorAlive && irValue < 5000) {
            IR_AC_Max = 20; IR_AC_Min = -20;
            IR_AC_Signal_Current = 0; IR_AC_Signal_Previous = 0;
            IR_AC_Signal_min = 0; IR_AC_Signal_max = 0;
            IR_Average_Estimated = 0;
            positiveEdge = 0; negativeEdge = 0;
            ir_avg_reg = 0; offset = 0;
            memset(cbuf, 0, sizeof(cbuf));
        } else if (gHrSensorAlive && checkForBeat(irValue) == true) {
            long delta = millis() - gLastBeatMs;
            gLastBeatMs = millis();

            float bpm = 60.0f / (delta / 1000.0f);

            if (bpm > 20.0f && bpm < 255.0f) {
                gHrBuffer[gHrBufIdx++] = bpm;
                gHrBufIdx %= 4;

                float avg = 0.0f;
                for (int i = 0; i < 4; i++) avg += gHrBuffer[i];
                float avgBpm = avg / 4.0f;

                if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    gState.heart_rate = avgBpm;
                    gState.timestamp = millis();
                    xSemaphoreGive(gStateMutex);
                }

                if (xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    Serial.printf("[HR] Beat detected: %.0f BPM (avg %.0f)\n", bpm, avgBpm);
                    xSemaphoreGive(gSerialMutex);
                }
            }
        }

        // --- SpO2 from red/IR DC ratio (Maxim AN6407) ---
        if (gHrSensorAlive) {
            gRedDC = DC_EMA_ALPHA * (float)redValue + (1.0f - DC_EMA_ALPHA) * gRedDC;
            gIrDC  = DC_EMA_ALPHA * (float)irValue + (1.0f - DC_EMA_ALPHA) * gIrDC;

            if (gIrDC > 1000.0f) {
                float rRatio = gRedDC / gIrDC;
                float spo2 = -45.060f * rRatio * rRatio + 30.354f * rRatio + 94.845f;
                gSpo2EMA = SPO2_EMA_ALPHA * spo2 + (1.0f - SPO2_EMA_ALPHA) * gSpo2EMA;

                if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    gState.spo2 = constrain(gSpo2EMA, 70.0f, 100.0f);
                    xSemaphoreGive(gStateMutex);
                }
            }
        }

        // --- MPU-9250: motion magnitude (every other cycle ≈ 12Hz) ---
        if (gMpuAlive) {
            mpuCycle++;
            if (mpuCycle % 2 == 0) {
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
            }
        }

        // 25Hz -> 40ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(40));
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

            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
            continue;
        }

        // --- Priority 2: Cooldown dino animation (from demo) ---
        if (gCooldownActive) {
            drawCooldownDinoScreen();
            lastDisplayCycle = millis();

            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
            continue;
        }

        // --- Priority 3: Normal metrics ---
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        // Title
        display.setCursor(0, 0);
        display.println("Senseware");
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
        // Dead sensors feed their calibration mean (z-score=0) to prevent
        // false anomaly triggers from zero/garbage readings.
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            hr  = gHrSensorAlive ? gState.heart_rate      : NORM_MEAN[0];
            emg = gState.emg_envelope;
            mot = gMpuAlive    ? gState.motion_magnitude : NORM_MEAN[2];
            xSemaphoreGive(gStateMutex);
        }

        // Normalize inputs: z-score
        input[0] = (hr - NORM_MEAN[0]) / NORM_STD[0];
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

                // Cap threshold to prevent runaway
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

        // Snapshot under mutex — release before Serial output
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

        // 1 Hz -> 1000 ms period
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}
