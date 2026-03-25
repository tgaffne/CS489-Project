#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 particleSensor;

/* NOTES FOR AIDAN AND TOMMY
 *
 * SERIAL - connection from board to laptop
 * SERIAL1 - another thing idk
 * SERIAL2 - GPS connection
 *
 * The HR sensor works over I2C. Those standard GPIO pins are SDA to 21 and SDL to 22
 */

/* GPS NOTES
 *
 * The GPS module spits out lines in this format
 * https://w3.cs.jmu.edu/bernstdh/web/common/help/nmea-sentences.php
 *
 */

void setup() {
  Serial.begin(115200);

  // --- GPS Setup ---
  Serial2.begin(9600, SERIAL_8N1, 16, 17);  // RX=16, TX=17


  // --- MAX30102 Setup ---
  Wire.begin(21, 22);  // SDA=21, SCL=22
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    // while (1);
  }

  particleSensor.setup();  // default settings
  Serial.println("MAX30102 started");
}

void loop() {
  Serial.print("made it to the loop");
  // --- Read GPS ---
  if (Serial2.available()) {
    String nmea = Serial2.readStringUntil('\n');
    Serial.print("GPS: ");
    Serial.println(nmea);
  }

  // --- Read MAX30102 ---
  long irValue = particleSensor.getIR();

  Serial.print("IR: ");
  Serial.println(irValue);

  // TEST
  byte error, address;
  int nDevices = 0;

  Serial.println("Scanning...");

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at 0x");
      Serial.println(address, HEX);
      nDevices++;
    }
  }

  if (nDevices == 0)
    Serial.println("No I2C devices found");
  else
    Serial.println("Scan done");

  delay(50);
}
