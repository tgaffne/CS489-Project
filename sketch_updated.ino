#include <Wire.h>
#include <LittleFS.h>
#include "DFRobot_BloodOxygen_S.h"

#define I2C_ADDRESS 0x57
DFRobot_BloodOxygen_S_I2C MAX30102(&Wire ,I2C_ADDRESS);

#define BUTTON_PIN 18
#define DOWNLOAD_BUTTON_PIN 23

bool lastDownloadState = HIGH;
bool hrSensorOK = true;

/* Global Variables */
unsigned long lastTime = 0;
unsigned long interval = 5000; // 5000 ms between loops

/* GPSPoint
 * 
 * Over each iteration we want to store the heart rate 
 * reading and GPS data
 */
typedef struct GPSPoint {
  float speedMPH;
  double latitude;
  double longitude;
  char time[16];
  char date[16];
} GPSPoint;

/* HRPoint
 *
 * Reads HR every iteration. For now, a struct in case we wanna add
 * blood oxygen data????
 */
typedef struct HRPoint {
  int hr;
} HRPoint;

/* Global HR data point, updated continuously */
GPSPoint globalGPS = {0}; 
HRPoint globalHR = {0};

/* Global Tracking var */
bool isTracking = false;
bool lastState = HIGH;

/* Global File var */
File globalFile;

/* NOTES FOR AIDAN AND TOMMY
 *
 * SERIAL - connection from board to laptop
 * SERIAL1 - another thing idk
 * SERIAL2 - GPS connection
 *
 * The HR sensor works over I2C. Those standard GPIO pins are SCL to 22 and SDA to 21  
 * 
 * The GPS module spits out lines in this format... use it as a reference
 * https://w3.cs.jmu.edu/bernstdh/web/common/help/nmea-sentences.php
 *
 */

/* convertToDecimal()
 *
 * coord: string reading from the NMEA buffer to be converted
 * direction: char to determine coordinate sign
 *
 * Raw coordinates are given in the form DD(D)MM.MMMM 
 * and must be converted to DD.DDDDD. 
 */
double convertToDecimal(const char *coord, char direction) {
    double raw = atof(coord);

    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);

    double decimal = degrees + (minutes / 60.0);

    if (direction == 'S' || direction == 'W') { decimal *= -1; }

    return decimal;
}

/* parseNMEA
 *
 * buf: Null terminated string reading from the GPS module
 * gps: GPSPoint struct to be filled with GPS readings
 */
void parseNMEA(char *buf, GPSPoint &gps) {
  if (strncmp(buf, "$GPRMC", 6) != 0) { return; } /* We only need to worry about this line for now */

  char *checksum = strstr(buf, "*");
  if (checksum) { *checksum = '\0'; } // Make it easier to end the line

  char *fields[20];
  int fieldNum = 0;

  // Collect all the fields
  char *itr = strtok(buf, ","); // Replace all ',' with '\0'
  while (fieldNum < 20 && itr != NULL) {
    fields[fieldNum++] = itr;
    itr = strtok(NULL, ",");
  }

  if (fieldNum < 3) return; /* Line must be incomplete */

  /* Datapoints are meaningless without any of these  */
  if (!fields[1] || !fields[2] || !fields[3] || !fields[4] || !fields[5] || !fields[6] || !fields[9] ) {
    Serial.println("Missing coordinate fields");
    return;
  }
  if (fields[2][0] != 'A') { Serial.println("No fix, skipping line"); return; }

  /* FILL STRUCT */
  strncpy(gps.time, fields[1], sizeof(gps.time));
  strncpy(gps.date, fields[9], sizeof(gps.date));

  float speedKnots = atof(fields[7]); /* Speed in knots, need to convert to mph */
  gps.speedMPH = speedKnots * 1.15078;

  /* Format the coordinates in the struct */
  gps.latitude = convertToDecimal(fields[3], fields[4][0]);
  gps.longitude = convertToDecimal(fields[5], fields[6][0]);
}

/* readGPS()
 *
 * gps: struct for a gps point to be filled.
 *
 * Called when polling the UART buffer for the GPS module.
 * If data is available, it will be filled int the 'gps' field
 *
 * Lines roughly appear as:
 *        $GP---,,,,,,,,,*\r\n
 */
void readGPS(GPSPoint &gps) {
  char buf[1024] = {0};

  // Increment the buffer, reset when a word terminates
  int itr = 0;
  while (Serial2.available()) {
    // I won't use readStringUntil("\n") here because it blocks
    // and sentences may be incomplete
    char c = Serial2.read();

    if (c == '\r') { continue; } /* Don't add to buffer */

    // Lines end with '\r\n'
    if (c == '\n') {
      buf[itr++] = '\0';

      parseNMEA(buf, gps);
      itr = 0;
    }
    else if (itr < sizeof(buf) - 1) { 
      buf[itr++] = c;
    }

    // If the buffer is full, then read all of the characters- it is
    // malformed anyways
  }
}

/* collectHR()
 *
 * hrpoint: hr struct to be filled from polling the HR sensor
 */
void collectHR(HRpoint &hrpoint) {
  // Try reading sensor

  MAX30102.sensorEndCollect();   // stop first (safe even if already stopped)
  delay(100);

  if (MAX30102.begin()) { /* Reconnect every time, can happen when pulled away from skin */
    MAX30102.sensorStartCollect();
    MAX30102.getHeartbeatSPO2();

    int hr = MAX30102._sHeartbeatSPO2.Heartbeat;
    if (hr > 0 && hr <= 250) {
      hrpoint.hr = hr;
    }
  } else { //Serial.println("Reconnect failed"); }
}

/* sendAllFilesOverSerial()
 *
 * Controlled by button press. One by one sends each file over USB serial to
 * the application.
 */
void sendAllFilesOverSerial() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("Failed to open root");
    return;
  }

  File file = root.openNextFile();

  while (file) {
    String name = file.name();
    //Serial.println(name);

    while (file.available()) {
      String line = file.readStringUntil('\n');
      Serial.println(line);
    }

    file.close();  // IMPORTANT
    file = root.openNextFile();
  }

  root.close();
  
  Serial.flush();
  
  delay(500);

  deleteAllFiles();
  Serial.println("END");
}

void setup() {
  /* Board */
  Serial.begin(115200);

  /* GPS Setup */
  Serial2.begin(9600, SERIAL_8N1, 16, 17);  // RX=16, TX=17

  /* LittleFS */
  if (!LittleFS.begin()) {
    LittleFS.format();
    LittleFS.begin(); 
  }

  /* MAX30102 Setup */
  while (false == MAX30102.begin())
  {
    Serial.println("init fail!");
    delay(1000);
  }
  Serial.println("init success!");
  Serial.println("start measuring...");
  MAX30102.sensorStartCollect();

  /* Button Setup */
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWNLOAD_BUTTON_PIN, INPUT_PULLUP);
}

void handleTime() {
  unsigned long currentTime = millis();
  unsigned long difference = currentTime - lastTime;

  // Serial.println(difference);
  if (difference < interval) { 
    // Serial.print("Waiting the difference... ");
    // Serial.println(interval - difference);
    delay(interval - difference); 
  }

  lastTime = millis();
}

/* makeFile()
 *
 * timestamp: A timestamp in milliseconds to take the name of the file
 * 
 */
File makeFile(unsigned long timestamp) {
  char filename[32];
  sprintf(filename, "/%lu.txt", timestamp);

  File file = LittleFS.open(filename, "w");

  if (!file) { Serial.println("Failed to create file!"); }
  else { Serial.println("Created File!"); Serial.println(filename); }

  return file;
}

/* Used when creating/saving from the record button */
void closeFile(File file) {
  if (file) { file.flush(); file.close(); }
}

void listFiles() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("Failed to open root directory");
  } else {
    File file = root.openNextFile();
    while (file) {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
      file = root.openNextFile();
    }
  }
}

void deleteAllFiles() {
  while (true) {
    File root = LittleFS.open("/");
    File file = root.openNextFile();

    if (!file) {
      root.close();
      break;
    }

    String name = file.path();
    file.close();
    root.close();

    Serial.print("Deleting: ");
    Serial.println(name);

    if (!LittleFS.remove(name.c_str())) {
      Serial.println("Delete failed!");
    }
  }
}

void handleButton() {
  bool current = digitalRead(BUTTON_PIN);
  bool downloadCurrent = digitalRead(DOWNLOAD_BUTTON_PIN);

  // --- Tracking button ---
  if (lastState == HIGH && current == LOW) {
    delay(50);

    if (!isTracking) {
      unsigned long timestamp = millis();
      globalFile = makeFile(timestamp);
      isTracking = true;
      Serial.println("Tracking started");
    } else {
      closeFile(globalFile);
      isTracking = false;
      Serial.println("Tracking stopped");
    }
  }

  // --- Download button ---
  if (lastDownloadState == HIGH && downloadCurrent == LOW) {
    delay(50);

    //Serial.println("Sending files over Serial...");
    sendAllFilesOverSerial();
  }

  lastState = current;
  lastDownloadState = downloadCurrent;
}

void loop() {
  /* Check For Button Press */
  handleButton();

  /* --- Delay if Needed --- */
  handleTime();

  /* --- Read GPS --- */
  readGPS(globalGPS);

  /* --- Read HR --- */
  collectHR(globalHR);

  if (isTracking) {
  //if (isTracking && globalGPS.hasFix) {
    String line = "";

    // Latitude with N/S
    line += String(globalGPS.latitude, 5);
    // dont add if struct empty
    line += ",";

    // Longitude with E/W
    line += String(globalGPS.longitude, 5);
    line += ",";

    // Speed
    line += String(globalGPS.speedMPH, 3); 
    line += ",";
    // Heart rate
    line += String(globalHR.hr);
    line += "\n";

    globalFile.print(line);
    globalFile.flush();

    Serial.println("Wrote: " + line);
    listFiles();
  }
}
