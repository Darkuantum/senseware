/*
 * Senseware I2C Bus Scanner
 * YD-ESP32 Type-A (ESP32-WROOM-32)
 * SDA: GPIO 21, SCL: GPIO 22
 *
 * Scans all 128 possible I2C addresses and reports which devices respond.
 * Expected devices:
 *   0x3C — SH1106 OLED
 *   0x57 — MAX30102 PPG
 *   0x68 — MPU-9250 IMU
 */

#include <Wire.h>

#define SDA_PIN 21
#define SCL_PIN 22

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.println("\n========================================");
  Serial.println("  Senseware I2C Bus Scanner");
  Serial.println("  SDA: GPIO 21  |  SCL: GPIO 22");
  Serial.println("========================================\n");

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100); // Small settle time after init

  uint8_t error, address;
  int deviceCount = 0;

  Serial.println("Scanning I2C bus...\n");

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("  FOUND device at 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);

      // Identify known devices
      switch (address) {
        case 0x3C:
          Serial.println("  <- SH1106 OLED");
          break;
        case 0x57:
          Serial.println("  <- MAX30102 PPG");
          break;
        case 0x68:
          Serial.println("  <- MPU-9250 IMU");
          break;
        case 0x69:
          Serial.println("  <- MPU-9250 IMU (alt addr)");
          break;
        default:
          Serial.println("  <- Unknown device");
          break;
      }
      deviceCount++;
    }
  }

  Serial.println();
  if (deviceCount == 0) {
    Serial.println("  *** No I2C devices found! ***");
    Serial.println("  Check wiring:");
    Serial.println("    SDA -> GPIO 21");
    Serial.println("    SCL -> GPIO 22");
    Serial.println("    VCC -> 3.3V");
    Serial.println("    GND -> GND");
  } else {
    Serial.print("  Scan complete. ");
    Serial.print(deviceCount);
    Serial.println(" device(s) found.\n");
  }

  Serial.println("========================================");
  Serial.println("  Scan finished. Reboot to scan again.");
  Serial.println("========================================");
}

void loop() {
  // Re-scan every 5 seconds so we can catch output after connecting monitor
  delay(5000);

  uint8_t error, address;
  int deviceCount = 0;

  Serial.println("\n----------------------------------------");
  Serial.println("Scanning I2C bus...\n");

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("  FOUND device at 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);

      switch (address) {
        case 0x3C:  Serial.println("  <- SH1106 OLED"); break;
        case 0x57:  Serial.println("  <- MAX30102 PPG"); break;
        case 0x68:  Serial.println("  <- MPU-9250 IMU"); break;
        case 0x69:  Serial.println("  <- MPU-9250 IMU (alt)"); break;
        default:    Serial.println("  <- Unknown device"); break;
      }
      deviceCount++;
    }
  }

  Serial.println();
  if (deviceCount == 0) {
    Serial.println("  *** No I2C devices found! ***");
  } else {
    Serial.print("  ");
    Serial.print(deviceCount);
    Serial.println(" device(s) found.");
  }
}
