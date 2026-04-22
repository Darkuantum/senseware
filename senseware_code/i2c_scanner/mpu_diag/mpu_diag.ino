/*
 * MPU-9250 Diagnostic Sketch
 * YD-ESP32 Type-A (ESP32-WROOM-32)
 * SDA: GPIO 21, SCL: GPIO 22
 *
 * Probes the MPU-9250 (and its AK8963 magnetometer) at the raw I2C
 * register level to identify why the MPU9250 library's setup() fails.
 *
 * Key registers:
 *   0x75  WHO_AM_I  — expects 0x71 (MPU-9250), 0x73 (MPU-9255), 0x70 (MPU-6500)
 *   0x68  PWR_MGMT_1
 *   0x6B  PWR_MGMT_2
 *   0x19  SMPLRT_DIV
 *   0x1A  MPU_CONFIG
 *   0x1B  GYRO_CONFIG
 *   0x1C  ACCEL_CONFIG
 *   0x1D  ACCEL_CONFIG2
 *   0x37  INT_PIN_CFG
 *   0x38  INT_ENABLE
 *   0x3A  INT_STATUS
 *   0x3B-0x40  ACCEL_XOUT_H..ACCEL_ZOUT_L  (6 bytes)
 *   0x41-0x42  TEMP_OUT_H..TEMP_OUT_L      (2 bytes)
 *   0x43-0x48  GYRO_XOUT_H..GYRO_ZOUT_L    (6 bytes)
 *
 * AK8963 magnetometer (accessed via MPU-9250's I2C bypass at address 0x0C):
 *   0x00  AK8963_WHO_AM_I — expects 0x48
 *   0x02  AK8963_ST1
 *   0x03-0x08  AK8963_XOUT_L..AK8963_ZOUT_H + ST2 (7 bytes)
 *   0x0A  AK8963_CNTL
 *   0x0C  AK8963_ASAX (factory calibration)
 */

#include <Wire.h>

#define SDA_PIN 21
#define SCL_PIN 22
#define MPU_ADDR    0x68
#define AK8963_ADDR 0x0C

// Register map
#define WHO_AM_I_MPU9250  0x75
#define PWR_MGMT_1        0x6B
#define PWR_MGMT_2        0x6C
#define SMPLRT_DIV        0x19
#define MPU_CONFIG        0x1A
#define GYRO_CONFIG       0x1B
#define ACCEL_CONFIG      0x1C
#define ACCEL_CONFIG2     0x1D
#define INT_PIN_CFG       0x37
#define INT_ENABLE        0x38
#define INT_STATUS        0x3A
#define USER_CTRL         0x6A
#define I2C_MST_CTRL      0x24
#define I2C_MST_STATUS    0x36

#define ACCEL_XOUT_H      0x3B

#define AK8963_WHO_AM_I   0x00
#define AK8963_ST1        0x02
#define AK8963_XOUT_L     0x03
#define AK8963_CNTL       0x0A
#define AK8963_ASAX       0x0C

// --- Raw I2C helpers (no library dependency) ---
uint8_t readReg(uint8_t devAddr, uint8_t reg) {
    Wire.beginTransmission(devAddr);
    Wire.write(reg);
    uint8_t err = Wire.endTransmission(false);
    if (err != 0 && err != 7) {  // 7 is stickbreaker's "ok with restart"
        Serial.printf("    [I2C err=%u on addr=0x%02X reg=0x%02X]\n", err, devAddr, reg);
        return 0xFF;
    }
    Wire.requestFrom(devAddr, (uint8_t)1);
    if (Wire.available()) return Wire.read();
    return 0xFF;
}

void writeReg(uint8_t devAddr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(devAddr);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission(true);
    if (err != 0) {
        Serial.printf("    [I2C write err=%u on addr=0x%02X reg=0x%02X val=0x%02X]\n",
                      err, devAddr, reg, val);
    }
}

void readBytes(uint8_t devAddr, uint8_t reg, uint8_t count, uint8_t* dest) {
    Wire.beginTransmission(devAddr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(devAddr, count);
    for (uint8_t i = 0; i < count && Wire.available(); i++) {
        dest[i] = Wire.read();
    }
}

void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) { ; }

    Serial.println("\n======================================================");
    Serial.println("  MPU-9250 Diagnostic Sketch");
    Serial.println("  SDA: GPIO 21  |  SCL: GPIO 22");
    Serial.println("======================================================\n");

    // --- I2C init ---
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setTimeout(50);
    delay(100);

    // --- Step 0: Full bus scan ---
    Serial.println("--- Step 0: I2C Bus Scan ---");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            found++;
            Serial.printf("  Device at 0x%02X", a);
            if (a == 0x3C) Serial.print(" (SH1106 OLED)");
            else if (a == 0x57) Serial.print(" (MAX30102 PPG)");
            else if (a == 0x68) Serial.print(" (MPU-9250 IMU)");
            else if (a == 0x69) Serial.print(" (MPU-9250 alt)");
            else if (a == 0x0C) Serial.print(" (AK8963 direct — bypass active?)");
            else Serial.print(" (UNKNOWN)");
            Serial.println();
        }
    }
    Serial.printf("  %d device(s) found.\n\n", found);

    // --- Step 1: Probe MPU at 0x68 ---
    Serial.println("--- Step 1: MPU-9250 Probe (0x68) ---");
    Wire.beginTransmission(MPU_ADDR);
    uint8_t probeErr = Wire.endTransmission();
    if (probeErr != 0) {
        Serial.printf("  FAIL: MPU not responding at 0x68 (err=%u)\n", probeErr);
        Serial.println("  → Check wiring, pull-ups, and power.\n");

        // Try alternate address
        Wire.beginTransmission(0x69);
        probeErr = Wire.endTransmission();
        if (probeErr == 0) {
            Serial.println("  MPU found at alternate address 0x69! AD0 pin is HIGH.");
            Serial.println("  → Use address 0x69 in mpu.setup(0x69)");
        } else {
            Serial.printf("  Also checked 0x69 (err=%u). No MPU found.\n", probeErr);
        }
        Serial.println("\n=== DIAGNOSTIC COMPLETE (no MPU on bus) ===");
        while (1) { delay(1000); }
    }
    Serial.println("  MPU ACK'd at 0x68. Good.\n");

    // --- Step 2: WHO_AM_I ---
    Serial.println("--- Step 2: WHO_AM_I Register (0x75) ---");
    uint8_t whoami = readReg(MPU_ADDR, WHO_AM_I_MPU9250);
    Serial.printf("  WHO_AM_I = 0x%02X", whoami);
    if (whoami == 0x71) Serial.print("  ← MPU-9250 (expected)");
    else if (whoami == 0x73) Serial.print("  ← MPU-9255");
    else if (whoami == 0x70) Serial.print("  ← MPU-6500");
    else if (whoami == 0xFF) Serial.print("  ← READ FAILED (bus error)");
    else if (whoami == 0x00) Serial.print("  ← READ RETURNED 0x00 (pull-down or stuck bus)");
    else Serial.print("  ← UNKNOWN CHIP");
    Serial.println();

    bool whoamiOk = (whoami == 0x71 || whoami == 0x73 || whoami == 0x70);
    if (!whoamiOk) {
        Serial.println("  *** WHO_AM_I mismatch! Library will reject this chip. ***\n");
    } else {
        Serial.println("  WHO_AM_I check passed.\n");
    }

    // --- Step 3: Power management ---
    Serial.println("--- Step 3: Power Management ---");
    uint8_t pwr1 = readReg(MPU_ADDR, PWR_MGMT_1);
    uint8_t pwr2 = readReg(MPU_ADDR, PWR_MGMT_2);
    Serial.printf("  PWR_MGMT_1 (0x6B) = 0x%02X", pwr1);
    if (pwr1 & 0x80) Serial.print("  [DEVICE RESET in progress]");
    if (pwr1 & 0x40) Serial.print("  [SLEEP mode ON]");
    if (pwr1 == 0x00) Serial.print("  [Idle — internal 8MHz osc]");
    if (pwr1 == 0x01) Serial.print("  [Auto-select PLL clock]");
    Serial.println();
    Serial.printf("  PWR_MGMT_2 (0x6C) = 0x%02X", pwr2);
    if (pwr2 != 0x00) Serial.print("  [Sensors may be disabled!]");
    Serial.println();

    // Wake up if sleeping
    if ((pwr1 & 0x40) || (pwr1 & 0x80)) {
        Serial.println("  → Waking up MPU...");
        writeReg(MPU_ADDR, PWR_MGMT_1, 0x00);
        delay(100);
        writeReg(MPU_ADDR, PWR_MGMT_1, 0x01);  // PLL clock
        delay(200);
        pwr1 = readReg(MPU_ADDR, PWR_MGMT_1);
        Serial.printf("  → PWR_MGMT_1 now = 0x%02X\n", pwr1);
    }
    Serial.println();

    // --- Step 4: Key register dump ---
    Serial.println("--- Step 4: Register Dump (pre-init) ---");
    const uint8_t regs[] = {
        SMPLRT_DIV, MPU_CONFIG, GYRO_CONFIG, ACCEL_CONFIG, ACCEL_CONFIG2,
        INT_PIN_CFG, INT_ENABLE, INT_STATUS, USER_CTRL, I2C_MST_CTRL
    };
    const char* regNames[] = {
        "SMPLRT_DIV", "MPU_CONFIG", "GYRO_CONFIG", "ACCEL_CONFIG", "ACCEL_CONFIG2",
        "INT_PIN_CFG", "INT_ENABLE", "INT_STATUS", "USER_CTRL", "I2C_MST_CTRL"
    };
    for (int i = 0; i < 10; i++) {
        uint8_t v = readReg(MPU_ADDR, regs[i]);
        Serial.printf("  0x%02X %-14s = 0x%02X (%3u) [", regs[i], regNames[i], v, v);
        Serial.print(v, BIN);
        Serial.println("]");
    }
    Serial.println();

    // --- Step 5: Enable I2C bypass for AK8963 ---
    Serial.println("--- Step 5: I2C Bypass for AK8963 ---");
    uint8_t intPinCfg = readReg(MPU_ADDR, INT_PIN_CFG);
    Serial.printf("  INT_PIN_CFG before = 0x%02X\n", intPinCfg);

    // Set I2C_BYPASS_EN (bit 1) and INT_ANYRD_2CLEAR (bit 4) = 0x22
    writeReg(MPU_ADDR, INT_PIN_CFG, 0x22);
    delay(10);
    intPinCfg = readReg(MPU_ADDR, INT_PIN_CFG);
    Serial.printf("  INT_PIN_CFG after  = 0x%02X", intPinCfg);
    if (intPinCfg & 0x02) Serial.print("  [BYPASS ENABLED]");
    else Serial.print("  *** BYPASS NOT SET ***");
    Serial.println();
    Serial.println();

    // --- Step 6: AK8963 probe ---
    Serial.println("--- Step 6: AK8963 Magnetometer Probe ---");

    // Check if AK8963 is directly accessible (bypass must be on)
    Wire.beginTransmission(AK8963_ADDR);
    uint8_t akProbe = Wire.endTransmission();
    Serial.printf("  AK8963 probe at 0x0C: err=%u", akProbe);
    if (akProbe == 0) {
        Serial.println("  [ACK received]");
    } else {
        Serial.println("  [NO ACK — magnetometer not reachable]");
        Serial.println("  *** Library needs AK8963 to succeed. ***");
        Serial.println("  Possible causes:");
        Serial.println("    - I2C bypass not enabled (INT_PIN_CFG bit 1)");
        Serial.println("    - Magnetometer disconnected / broken");
        Serial.println("    - Wrong chip (MPU-6500 has no AK8963)");
        Serial.println();
        Serial.println("=== DIAGNOSTIC: AK8963 not reachable ===");
    }

    uint8_t akWhoAmI = 0;
    if (akProbe == 0) {
        akWhoAmI = readReg(AK8963_ADDR, AK8963_WHO_AM_I);
        Serial.printf("  AK8963 WHO_AM_I (0x00) = 0x%02X", akWhoAmI);
        if (akWhoAmI == 0x48) Serial.print("  ← AK8963 (expected)");
        else if (akWhoAmI == 0xFF) Serial.print("  ← READ FAILED");
        else Serial.print("  ← UNEXPECTED");
        Serial.println();

        // Read ST1
        uint8_t st1 = readReg(AK8963_ADDR, AK8963_ST1);
        Serial.printf("  AK8963 ST1 = 0x%02X\n", st1);

        // Read CNTL (power mode)
        uint8_t cntl = readReg(AK8963_ADDR, AK8963_CNTL);
        Serial.printf("  AK8963 CNTL = 0x%02X", cntl);
        if (cntl == 0x00) Serial.print(" (power-down)");
        else if (cntl == 0x02) Serial.print(" (continuous 8Hz)");
        else if (cntl == 0x06) Serial.print(" (continuous 100Hz)");
        else if (cntl == 0x0F) Serial.print(" (Fuse ROM access)");
        Serial.println();

        // Try reading factory calibration (needs Fuse ROM mode)
        Serial.println("  Attempting to read AK8963 factory calibration...");
        writeReg(AK8963_ADDR, AK8963_CNTL, 0x00);  // power down
        delay(10);
        writeReg(AK8963_ADDR, AK8963_CNTL, 0x0F);  // fuse ROM
        delay(10);
        uint8_t asax = readReg(AK8963_ADDR, AK8963_ASAX);
        uint8_t asay = readReg(AK8963_ADDR, AK8963_ASAX + 1);
        uint8_t asaz = readReg(AK8963_ADDR, AK8963_ASAX + 2);
        Serial.printf("  ASAX=%u  ASAY=%u  ASAZ=%u\n", asax, asay, asaz);
        writeReg(AK8963_ADDR, AK8963_CNTL, 0x00);  // power down again
        delay(10);
    }
    Serial.println();

    // --- Step 7: Manual init + accel data read ---
    Serial.println("--- Step 7: Manual MPU Init + Accel Data Read ---");

    // Reset
    writeReg(MPU_ADDR, PWR_MGMT_1, 0x80);
    delay(100);
    writeReg(MPU_ADDR, PWR_MGMT_1, 0x01);
    delay(200);

    // Configure
    writeReg(MPU_ADDR, MPU_CONFIG, 0x01);   // 188Hz gyro DLPF
    writeReg(MPU_ADDR, SMPLRT_DIV, 0x04);   // 200Hz sample rate
    writeReg(MPU_ADDR, GYRO_CONFIG, 0x18);  // 2000dps
    writeReg(MPU_ADDR, ACCEL_CONFIG, 0x18); // 16g
    writeReg(MPU_ADDR, ACCEL_CONFIG2, 0x01); // 1kHz accel, 184Hz BW

    // Enable bypass for mag
    writeReg(MPU_ADDR, INT_PIN_CFG, 0x22);
    writeReg(MPU_ADDR, INT_ENABLE, 0x01);   // data ready interrupt
    delay(100);

    // Read accel/gyro/temperature (14 bytes from 0x3B)
    uint8_t rawData[14];
    readBytes(MPU_ADDR, ACCEL_XOUT_H, 14, rawData);

    int16_t ax = ((int16_t)rawData[0] << 8) | rawData[1];
    int16_t ay = ((int16_t)rawData[2] << 8) | rawData[3];
    int16_t az = ((int16_t)rawData[4] << 8) | rawData[5];
    int16_t temp = ((int16_t)rawData[6] << 8) | rawData[7];
    int16_t gx = ((int16_t)rawData[8] << 8) | rawData[9];
    int16_t gy = ((int16_t)rawData[10] << 8) | rawData[11];
    int16_t gz = ((int16_t)rawData[12] << 8) | rawData[13];

    float accelMag = sqrtf((float)ax*ax + (float)ay*ay + (float)az*az) / 2048.0f;
    float tempC = (float)temp / 333.87f + 21.0f;

    Serial.printf("  Accel X=%+6d  Y=%+6d  Z=%+6d  |mag|=%.2fg\n", ax, ay, az, accelMag);
    Serial.printf("  Gyro  X=%+6d  Y=%+6d  Z=%+6d\n", gx, gy, gz);
    Serial.printf("  Temp  raw=%6d  %.1f°C\n", temp, tempC);

    bool accelOk = (ax != 0 || ay != 0 || az != 0);
    Serial.printf("  Accel non-zero: %s\n", accelOk ? "YES" : "NO (all zeros!)");
    if (accelMag > 0.5f && accelMag < 2.0f) {
        Serial.println("  Accel magnitude reasonable (~1g) — sensor is working.\n");
    } else if (accelMag > 0.0f && accelOk) {
        Serial.printf("  Accel magnitude %.2fg — outside typical 1g range but sensor alive.\n", accelMag);
    } else {
        Serial.println("  *** Accel magnitude is 0 or invalid — sensor may not be working. ***\n");
    }

    // --- Step 8: INT_STATUS check ---
    Serial.println("--- Step 8: Data Ready Interrupt ---");
    uint8_t intStat = readReg(MPU_ADDR, INT_STATUS);
    Serial.printf("  INT_STATUS = 0x%02X", intStat);
    if (intStat & 0x01) Serial.print("  [DATA READY]");
    else Serial.print("  [no data ready]");
    Serial.println();
    Serial.println();

    // --- Step 9: Try library setup with verbose mode ---
    Serial.println("--- Step 9: Library setup() with verbose=true ---");
    Serial.println("  (skipped in this raw-diagnostic sketch — see output above)\n");

    // --- Summary ---
    Serial.println("======================================================");
    Serial.println("  DIAGNOSTIC SUMMARY");
    Serial.println("======================================================");
    Serial.printf("  WHO_AM_I       : 0x%02X  %s\n", whoami,
        whoamiOk ? "[OK]" : "*** FAIL ***");
    Serial.printf("  MPU ACK        : %s\n",
        (probeErr == 0) ? "[OK]" : "*** FAIL ***");
    Serial.printf("  Accel data     : %s\n",
        accelOk ? "[non-zero]" : "*** ALL ZEROS ***");
    Serial.printf("  Accel magnitude: %.2fg %s\n", accelMag,
        (accelMag > 0.5f && accelMag < 2.0f) ? "[~1g, OK]" : "[suspicious]");
    Serial.printf("  AK8963 probe   : %s\n",
        (akProbe == 0) ? "[ACK]" : "*** NO ACK ***");
    Serial.printf("  AK8963 WHO_AM_I: 0x%02X %s\n", akWhoAmI,
        (akWhoAmI == 0x48) ? "[OK]" : (akProbe == 0 ? "*** WRONG ID ***" : "[N/A]"));

    if (whoamiOk && akProbe == 0 && akWhoAmI == 0x48) {
        Serial.println("\n  => Both MPU and AK8963 look healthy.");
        Serial.println("     Library setup() should succeed.");
    } else if (whoamiOk && (akProbe != 0 || akWhoAmI != 0x48)) {
        Serial.println("\n  => MPU chip OK but AK8963 (magnetometer) FAILED.");
        Serial.println("     Library setup() returns false because isConnectedAK8963() fails.");
        Serial.println("     This is a known failure mode — see comments in code.");
    } else {
        Serial.println("\n  => MPU chip itself not detected correctly.");
        Serial.println("     Check wiring, I2C pull-ups, and power supply.");
    }

    Serial.println("\n======================================================");
    Serial.println("  DIAGNOSTIC COMPLETE");
    Serial.println("======================================================");
}

void loop() {
    // Nothing — one-shot diagnostic
    delay(10000);
}
