#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "MPU9250.h"
#include "DFRobot_BloodOxygen_S.h"
#include "model_data.h"
#include "EMGFilters.h"

#include <Chirale_TensorFlowLite.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// ================= PIN DEFINITIONS =================
#define EMG_PIN       34
#define VIB_PIN       25
#define I2C_SDA       21
#define I2C_SCL       22
#define I2C_ADDR_MPU  0x68
#define HR_ADDR       0x57

// ================= MODEL CONFIG =================
#define NUM_FEATURES 3
#define NUM_OUTPUTS  3
#define TENSOR_ARENA_SIZE (8 * 1024)

const float NORM_MEAN[NUM_FEATURES] = {71.6044f, 23.9010f, 0.9839f};
const float NORM_STD[NUM_FEATURES]  = {4.7835f, 15.1257f, 0.1589f};

float anomalyThreshold = 0.00152f;
const int ANOMALY_CONFIRM_COUNT = 3;

// ================= OBJECTS =================
MPU9250 mpu;
DFRobot_BloodOxygen_S_I2C bloodOxygen(&Wire, HR_ADDR);
Adafruit_SH1106G display(128, 64, &Wire, -1);
EMGFilters emgFilter;

// ================= TFLITE GLOBALS =================
tflite::MicroErrorReporter gMicroErrorReporter;
tflite::ErrorReporter* gErrorReporter = &gMicroErrorReporter;
const tflite::Model* gModel = nullptr;
tflite::MicroInterpreter* gInterpreter = nullptr;
TfLiteTensor* gInput = nullptr;
TfLiteTensor* gOutput = nullptr;
alignas(16) uint8_t gTensorArena[TENSOR_ARENA_SIZE];
bool gModelReady = false;

// ================= SENSOR STATE =================
float emgEnvelope = 0.0f;
float motionMagnitude = 0.0f;
float heartRateForModel = 71.6044f;
float displayedHeartRate = 0.0f;
float displayedSpO2 = 0.0f;
float accuracyPercent = 0.0f;

// ================= EMG CONFIG =================
// ADC² convention: EMGFilters bandpass → sq() → EMA smooth
const float EMG_EMA_ALPHA = 0.01f;
const float EMG_TRIGGER_THRESHOLD = 200.0f;  // debug-only (ADC² units)
int emgTriggerCounter = 0;

// ================= HAPTIC =================
const int HAPTIC_DUTY = 110;
const unsigned long HAPTIC_PULSE_MS = 220;
const unsigned long HAPTIC_GAP_MS = 180;

bool vibrationSequenceActive = false;
bool vibrationMotorOn = false;
unsigned long vibrationStepStart = 0;
int vibrationPulsesRemaining = 0;

// ================= ALERT BLINK =================
bool alertBlinkActive = false;
unsigned long alertBlinkStart = 0;
const unsigned long ALERT_BLINK_INTERVAL_MS = 200;
const int ALERT_BLINK_TOGGLES = 6; // 3 blinks

// ================= ANOMALY STATE =================
int anomalyCounter = 0;

// 5-second detection window + 15-second cooldown
const unsigned long ANOMALY_WINDOW_MS = 5000;
const unsigned long ANOMALY_COOLDOWN_MS = 15000;

bool windowActive = false;
unsigned long windowStartMs = 0;
int anomalyEventCount = 0;
bool prevImuAnomaly = false;
unsigned long continuousAnomalyStartMs = 0;

bool cooldownActive = false;
unsigned long cooldownStartMs = 0;

// ================= FUNCTIONS =================
void initModel() {
  gModel = tflite::GetModel(senseware_model_tflite);

  if (gModel->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema mismatch.");
    return;
  }

  static tflite::AllOpsResolver resolver;
  static tflite::MicroInterpreter staticInterpreter(
    gModel, resolver, gTensorArena, TENSOR_ARENA_SIZE
  );

  gInterpreter = &staticInterpreter;

  if (gInterpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed.");
    return;
  }

  gInput = gInterpreter->input(0);
  gOutput = gInterpreter->output(0);
  gModelReady = true;

  Serial.println("ML model ready.");
}

void updateHeartSensor() {
  bloodOxygen.getHeartbeatSPO2();

  int rawHR = bloodOxygen._sHeartbeatSPO2.Heartbeat;
  int rawSpO2 = bloodOxygen._sHeartbeatSPO2.SPO2;

  if (rawHR > 0) {
    displayedHeartRate = rawHR;
    heartRateForModel = rawHR;
  } else {
    displayedHeartRate = 0.0f;
    heartRateForModel = NORM_MEAN[0];
  }

  if (rawSpO2 > 0) {
    displayedSpO2 = rawSpO2;
  } else {
    displayedSpO2 = 0.0f;
  }
}

void updateEMG() {
  int raw = analogRead(EMG_PIN);
  int filtered = emgFilter.update(raw);
  float sqVal = (float)(filtered * filtered);
  emgEnvelope = EMG_EMA_ALPHA * sqVal + (1.0f - EMG_EMA_ALPHA) * emgEnvelope;
}

void updateMPU() {
  if (mpu.update()) {
    float ax = mpu.getAccX();
    float ay = mpu.getAccY();
    float az = mpu.getAccZ();
    motionMagnitude = sqrtf(ax * ax + ay * ay + az * az);
  }
}

float runAnomalyModel() {
  if (!gModelReady) return -1.0f;

  float input[NUM_FEATURES];
  input[0] = (heartRateForModel - NORM_MEAN[0]) / NORM_STD[0];
  input[1] = (emgEnvelope      - NORM_MEAN[1]) / NORM_STD[1];
  input[2] = (motionMagnitude  - NORM_MEAN[2]) / NORM_STD[2];

  for (int i = 0; i < NUM_FEATURES; i++) {
    gInput->data.f[i] = input[i];
  }

  if (gInterpreter->Invoke() != kTfLiteOk) {
    Serial.println("Inference failed.");
    return -1.0f;
  }

  float mse = 0.0f;
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    float diff = input[i] - gOutput->data.f[i];
    mse += diff * diff;
  }
  mse /= NUM_OUTPUTS;

  return mse;
}

float computeAccuracyPercent(float mse) {
  if (mse < 0.0f) return 0.0f;
  float denom = max(anomalyThreshold, 0.000001f);
  float acc = 100.0f * expf(-mse / denom);
  return constrain(acc, 0.0f, 100.0f);
}

void startDoubleVibration() {
  vibrationSequenceActive = true;
  vibrationMotorOn = true;
  vibrationStepStart = millis();
  vibrationPulsesRemaining = 2;

  ledcWrite(VIB_PIN, HAPTIC_DUTY * 0.8); // slightly softer start

  alertBlinkActive = true;
  alertBlinkStart = millis();
}

void updateVibration() {
  if (!vibrationSequenceActive) return;

  unsigned long now = millis();

  if (vibrationMotorOn) {
    if (now - vibrationStepStart >= HAPTIC_PULSE_MS) {
      ledcWrite(VIB_PIN, 0);
      vibrationMotorOn = false;
      vibrationStepStart = now;
      vibrationPulsesRemaining--;

      if (vibrationPulsesRemaining <= 0) {
        vibrationSequenceActive = false;
      }
    }
  } else {
    if (vibrationPulsesRemaining > 0 && now - vibrationStepStart >= HAPTIC_GAP_MS) {
      ledcWrite(VIB_PIN, HAPTIC_DUTY);
      vibrationMotorOn = true;
      vibrationStepStart = now;
    }
  }
}

bool shouldShowAlertText() {
  if (!alertBlinkActive) return false;

  unsigned long elapsed = millis() - alertBlinkStart;
  unsigned long totalDuration = ALERT_BLINK_INTERVAL_MS * ALERT_BLINK_TOGGLES;

  if (elapsed >= totalDuration) {
    alertBlinkActive = false;
    return false;
  }

  unsigned long phase = elapsed / ALERT_BLINK_INTERVAL_MS;
  return (phase % 2 == 0);
}

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

void updateDisplay(float mse, bool anomaly) {
  display.clearDisplay();

  // ALERT has highest priority
  if (shouldShowAlertText()) {
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
    return;
  }

  // Then cooldown dino
  if (cooldownActive) {
    drawCooldownDinoScreen();
    return;
  }

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);

  display.println("Senseware ML Test");
  display.println("----------------");

  display.print("HR: ");
  display.println(displayedHeartRate);

  display.print("EMG: ");
  display.println(emgEnvelope, 3);

  display.print("ACC: ");
  display.println(accuracyPercent, 1);

  display.print("MOT: ");
  display.println(motionMagnitude, 3);

  display.print("MSE: ");
  display.println(mse, 5);

  display.print("ANOM: ");
  display.println(anomaly ? "YES" : "NO");

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Booting...");

  Wire.begin(I2C_SDA, I2C_SCL);

  analogReadResolution(12);
  analogSetPinAttenuation(EMG_PIN, ADC_11db);

  if (!mpu.setup(I2C_ADDR_MPU)) {
    Serial.println("MPU failed.");
  } else {
    Serial.println("MPU ready.");
  }

  if (!bloodOxygen.begin()) {
    Serial.println("HR sensor init failed (keeping placeholder mode).");
  } else {
    bloodOxygen.sensorStartCollect();
    Serial.println("HR sensor ready.");
  }

  if (!display.begin(0x3C, true)) {
    Serial.println("OLED failed.");
  } else {
    display.clearDisplay();
    display.display();
    Serial.println("OLED ready.");
  }

  ledcAttach(VIB_PIN, 5000, 8);
  ledcWrite(VIB_PIN, 0);

  // EMG filter init + warm-up (settles IIR filter states)
  emgFilter.init(SAMPLE_FREQ_1000HZ, NOTCH_FREQ_50HZ, true, true, true);
  Serial.println("Warming up EMG filter...");
  for (int i = 0; i < 2000; i++) {
    emgFilter.update(analogRead(EMG_PIN));
    delay(1);
  }
  emgEnvelope = 0.0f;
  Serial.println("EMG warm-up complete.");

  initModel();

  // make sure startup state is clean
  vibrationSequenceActive = false;
  vibrationMotorOn = false;
  alertBlinkActive = false;
  cooldownActive = false;
  windowActive = false;
  anomalyEventCount = 0;
  prevImuAnomaly = false;
  continuousAnomalyStartMs = 0;

  Serial.println("System ready.");
}

void loop() {
  updateHeartSensor();
  updateEMG();
  updateMPU();
  updateVibration();

  float mse = runAnomalyModel();
  accuracyPercent = computeAccuracyPercent(mse);

  bool anomaly = false;

  // EMG remains debug-only
  if (emgEnvelope > EMG_TRIGGER_THRESHOLD) {
    emgTriggerCounter++;
  } else {
    emgTriggerCounter = 0;
  }

  // model anomaly state
  bool imuAnomalyNow = false;
  if (!cooldownActive) {
    if (mse >= 0.0f && mse > anomalyThreshold) {
      anomalyCounter++;
    } else {
      anomalyCounter = 0;
    }
    imuAnomalyNow = (anomalyCounter >= ANOMALY_CONFIRM_COUNT);
  } else {
    anomalyCounter = 0;
  }

  unsigned long now = millis();

  if (cooldownActive) {
    if (now - cooldownStartMs >= ANOMALY_COOLDOWN_MS) {
      cooldownActive = false;
      windowActive = false;
      anomalyEventCount = 0;
      prevImuAnomaly = false;
      continuousAnomalyStartMs = 0;
    }
  } else {
    // start a new 5s window on first anomaly
    if (!windowActive && imuAnomalyNow) {
      windowActive = true;
      windowStartMs = now;
      anomalyEventCount = 1;
      continuousAnomalyStartMs = now;
      prevImuAnomaly = true;
    }

    if (windowActive) {
      if (imuAnomalyNow && !prevImuAnomaly) {
        anomalyEventCount++;
        continuousAnomalyStartMs = now;
      }

      if (!imuAnomalyNow) {
        continuousAnomalyStartMs = 0;
      }

      bool continuousForWholeWindow =
        imuAnomalyNow &&
        continuousAnomalyStartMs > 0 &&
        (now - continuousAnomalyStartMs >= ANOMALY_WINDOW_MS);

      bool windowExpired = (now - windowStartMs >= ANOMALY_WINDOW_MS);

      if (continuousForWholeWindow || (windowExpired && anomalyEventCount >= 3)) {
        anomaly = true;
        startDoubleVibration();

        cooldownActive = true;
        cooldownStartMs = now;

        windowActive = false;
        anomalyEventCount = 0;
        prevImuAnomaly = false;
        continuousAnomalyStartMs = 0;
      } else if (windowExpired) {
        windowActive = false;
        anomalyEventCount = 0;
        prevImuAnomaly = false;
        continuousAnomalyStartMs = 0;
      } else {
        anomaly = imuAnomalyNow;
        prevImuAnomaly = imuAnomalyNow;
      }
    } else {
      anomaly = imuAnomalyNow;
      prevImuAnomaly = imuAnomalyNow;
    }
  }

  Serial.print("HR_used=");
  Serial.print(heartRateForModel, 2);
  Serial.print(" EMG=");
  Serial.print(emgEnvelope, 4);
  Serial.print(" ACC=");
  Serial.print(accuracyPercent, 1);
  Serial.print(" MOT=");
  Serial.print(motionMagnitude, 4);
  Serial.print(" MSE=");
  Serial.print(mse, 6);
  Serial.print(" CNT=");
  Serial.print(anomalyCounter);
  Serial.print(" EVTS=");
  Serial.print(anomalyEventCount);
  Serial.print(" WIN=");
  Serial.print(windowActive ? 1 : 0);
  Serial.print(" COOL=");
  Serial.print(cooldownActive ? 1 : 0);
  Serial.print(" ANOM=");
  Serial.println(anomaly ? 1 : 0);

  updateDisplay(mse, anomaly);

  delay(20);
}
