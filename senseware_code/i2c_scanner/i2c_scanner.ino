#include <Wire.h>
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Wire.begin(21, 22);
  delay(100);
  Serial.println("Board recovered.");
}
void loop() { delay(5000); }
