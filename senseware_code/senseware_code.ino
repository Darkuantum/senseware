#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "MPU9250.h"
#include "MAX30105.h"

// Define pins
#define EMG_PIN 34
#define VIB_PIN 25

#define I2C_SDA 21
#define I2C_SCL 22

// --- Display Settings ---
#define i2c_Address 0x3c // Standard address for this module
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1    // QT-PY / ESP32 usually don't have a reset pin

const int pwmChannel = 0, pwmFreq = 5000, pwmResolution = 8;
MAX30105 particleSensor;
MPU9250 mpu;
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/*
#################
TEST SENSOR FUNCTIONS
#################
*/

void mpuTest() {
  if (mpu.update()) {
    Serial.print("Accel_X: "); Serial.print(mpu.getAccX(), 2);
    Serial.print(" Y: "); Serial.print(mpu.getAccY(), 2);
    Serial.print(" Z: "); Serial.print(mpu.getAccZ(), 2);
    
    Serial.print(" | Gyro_X: "); Serial.print(mpu.getGyroX(), 2);
    Serial.print(" Y: "); Serial.print(mpu.getGyroY(), 2);
    Serial.print(" Z: "); Serial.println(mpu.getGyroZ(), 2);
  }
  delay(50);
}

void heartTest() {
  Serial.print(" IR: ");
  Serial.print(particleSensor.getIR());
  Serial.print(" RED: ");
  Serial.print(particleSensor.getRed());
  Serial.println();
  delay(10); // Run at ~100Hz
}

void vibTest() {
  Serial.println("Fade In...");
  for (int dutyCycle = 0; dutyCycle <= 200; dutyCycle += 5) {
    ledcWrite(VIB_PIN, dutyCycle);
    delay(20);
  }
  
  Serial.println("Fade Out...");
  for (int dutyCycle = 200; dutyCycle >= 0; dutyCycle -= 5) {
    ledcWrite(VIB_PIN, dutyCycle);
    delay(20);
  }
  
  Serial.println("Rest...");
  delay(2000);
}

void emgTest() {
  int emgValue = analogRead(EMG_PIN);
  
  // Format for Serial Plotter
  Serial.print("Raw_EMG:");
  Serial.println(emgValue);
  
  delay(10);
}

void oledTest() {
  // Clear the buffer for the new frame
  display.clearDisplay();

  // Set text properties
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  // Print Title
  display.setCursor(0, 0);
  display.println("SenseWare UI Test");
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);

  // Print simulated data
  display.setCursor(0, 20);
  display.println("HR: 72 BPM");
  
  display.setCursor(0, 35);
  display.println("State: CALM");

  // Draw a simulated UI element (like a battery or stress gauge)
  display.drawRect(80, 20, 40, 10, SH110X_WHITE);
  display.fillRect(82, 22, 20, 6, SH110X_WHITE); // 50% full

  // Push the buffer to the physical screen
  display.display();

  delay(2000); // Wait 2 seconds

  // Flash a visual alert
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(20, 25);
  display.println("ALERT!");
  display.display();
  
  delay(1000);
}

/*
#################
SETUP SENSOR FUNCTIONS
#################
*/

void i2cSetup() {
  Wire.begin(I2C_SDA, I2C_SCL);

  /*
    MPU Setup
  */
  if (!mpu.setup(0x68)) { 
    while (1) {
      Serial.println("MPU connection failed. Please check your connection.");
      delay(1000);
    }
  }
  Serial.println("MPU-9250 initialized.");

  /*
    Heart Sensor Setup
  */
  Serial.println("Initializing MAX30102...");

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST, 0x57)) {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
    while (1);
  }

  Serial.println("Initializing SH1106 OLED...");

  /*
    OLED Screen Setup
  */
  // The 'true' parameter resets the display via software
  if (!display.begin(i2c_Address, true)) {
    Serial.println("SH1106 allocation failed. Check wiring or add external pull-ups!");
    while (1); // Halt execution if screen isn't found
  }

  Serial.println("Display found and initialized.");

  // Clear the display buffer (it usually contains an Adafruit splash screen)
  display.clearDisplay();
  display.display();
  delay(1000);

  // Setup to sense up to 18 inches, max LED brightness
  byte ledBrightness = 60; // Options: 0=Off to 255=50mA
  byte sampleAverage = 4; // Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; // Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  int sampleRate = 100; // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; // Options: 69, 118, 215, 411
  int adcRange = 4096; // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); 
  Serial.println("Setup complete. Place finger on sensor.");
}

void vibSetup() {
  ledcAttach(VIB_PIN, pwmFreq, pwmResolution);
  Serial.println("Vibration motor setup");
}

void emgSetup() {
  // Configure the ADC 
  analogReadResolution(12); // 0-4095
  analogSetPinAttenuation(EMG_PIN, ADC_11db); // Allows reading up to ~3.3V
  Serial.println("EMG Ready. Flex muscle!");
}

void setup() {
  Serial.begin(115200);
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);

  // i2cSetup();
  emgSetup();
  // svibSetup();
}

/*
#################
MAIN LOOP
#################
*/

void loop() {
  // mpuTest();
  // heartTest();
  // vibTest();
  emgTest();
  // oledTest();
}