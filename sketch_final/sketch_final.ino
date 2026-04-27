#include <Wire.h>
#include <LittleFS.h>
#include "DFRobot_BloodOxygen_S.h"

// http://ip/download-all


#define I2C_COMMUNICATION  //use I2C for communication, but use the serial port for communication if the line of codes were masked

#ifdef  I2C_COMMUNICATION
#define I2C_ADDRESS    0x57
  DFRobot_BloodOxygen_S_I2C MAX30102(&Wire ,I2C_ADDRESS);
#else
/* ---------------------------------------------------------------------------------------------------------------
 *    board   |             MCU                | Leonardo/Mega2560/M0 |    UNO    | ESP8266 | ESP32 |  microbit  |
 *     VCC    |            3.3V/5V             |        VCC           |    VCC    |   VCC   |  VCC  |     X      |
 *     GND    |              GND               |        GND           |    GND    |   GND   |  GND  |     X      |
 *     RX     |              TX                |     Serial1 TX1      |     5     |   5/D6  |  D2   |     X      |
 *     TX     |              RX                |     Serial1 RX1      |     4     |   4/D7  |  D3   |     X      |
 * ---------------------------------------------------------------------------------------------------------------*/
#if defined(ARDUINO_AVR_UNO) || defined(ESP8266)
SoftwareSerial mySerial(4, 5);
DFRobot_BloodOxygen_S_SoftWareUart MAX30102(&mySerial, 9600);
#else
DFRobot_BloodOxygen_S_HardWareUart MAX30102(&Serial1, 9600); 
#endif
#endif

#define BUTTON_PIN 18
#define DOWNLOAD_BUTTON_PIN 23

bool lastDownloadState = HIGH;
bool hrSensorOK = true;

/* Global Variables */
unsigned long lastTime = 0;
unsigned long interval = 5000; // 5000 ms between loops

/* Over each iteration we want to store the heart rate 
 * reading and GPS data
 *
 */
typedef struct GPSPoint {
  bool hasFix = false;

  char time[16];
  double latitude;
  double longitude;
  char date[16];
  float speedMPH;
} GPSPoint;

/* Global HR data point, updated continuously */
int globalHR = 0;
/* Global GPSPoint to be continuously updated */
GPSPoint globalGPS = {0}; 
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
 */

/* GPS NOTES
 *
 * The GPS module spits out lines in this format
 * https://w3.cs.jmu.edu/bernstdh/web/common/help/nmea-sentences.php
 *
 */

/* parseNMEA
 *
 * buf: Null terminated string reading from the GPS module
 * dp: GPSPoint struct to be filled with GPS readings
 */
double convertToDecimal(const char *coord, char direction) {
    double raw = atof(coord);

    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);

    double decimal = degrees + (minutes / 60.0);

    if (direction == 'S' || direction == 'W') {
        decimal *= -1;
    }

    return decimal;
}

void parseNMEA(char *buf, GPSPoint &gps) {
  // For now just do GPGMC
  // Serial.println(buf);
  if (strncmp(buf, "$GPRMC", 6) != 0) { return; }

  Serial.println("Got a GPGMC Line");
  Serial.println(buf);

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

  if (fieldNum < 3) return;  // need at least fields[0], fields[1], fields[2]
  if (fields[2] == NULL) return;
  if (fields[1] == NULL) return;
  if (fields[9] == NULL) return;



  // If count < 7 return for invalid?


  gps.hasFix = (fields[2][0] == 'A');

  // Collect these anyways?
  strncpy(gps.time, fields[1], sizeof(gps.time));
  strncpy(gps.date, fields[9], sizeof(gps.date));

  if (fields[2][0] != 'A') { Serial.println("No fix, skipping line"); return; }

  if (!fields[3] || !fields[4] || !fields[5] || !fields[6]) {
      Serial.println("Missing coordinate fields");
      return;
  }

  // Extract speed
  // fields[7] = speed in knots
  float speedKnots = atof(fields[7]);

  // Convert if you want:
  gps.speedMPH = speedKnots * 1.15078;

  // Get the coords
  gps.latitude = convertToDecimal(fields[3], fields[4][0]);
  gps.longitude = convertToDecimal(fields[5], fields[6][0]);

  // This one gives date, speed, course, ground speed which GGA doesn't
  // 1 - sentence type, skip and start at [6]
  // *2 - current time in UTF
  // 3 - position static, A for valid V for invalid
  // *4 - latitude
  // *5 - latitude compass
  // *6 - longitude
  // *7 - longitude compass
  // 8 - speed knots/hour
  // 9 - heading
  // *10 - date
  // 11 - magnet thing
  // 12 - magnet thing 2
  // 13 - checksum in hex
}

/* Call to read NMEA sentences */
void readGPS(GPSPoint &gps) {
  // Potential heuristic: check first three letters and imm
  char buf[1024] = {0};

  // Increment the buffer, reset when a word terminates
  int itr = 0;
  while (Serial2.available()) {
    char c = Serial2.read(); // Don't use readStringUntil("\n") because it blocks

    if (c == '\r') { continue; }

    // Lines end with '\r\n'
    if (c == '\n') {
      buf[itr++] = '\0';

      // DUBUG PRINT
      parseNMEA(buf, gps);
      itr = 0;
    }
    else if (itr < sizeof(buf) - 1) { // don't save carriage return
      buf[itr++] = c;
    }
  }
}

void collectHR() {
  // Try reading sensor

  MAX30102.sensorEndCollect();   // stop first (safe even if already stopped)
  delay(100);

  if (MAX30102.begin()) {
    //Serial.println("Reconnected to MAX30102");
    MAX30102.sensorStartCollect();
    MAX30102.getHeartbeatSPO2();

    int hr = MAX30102._sHeartbeatSPO2.Heartbeat;
    if (hr > 0 && hr <= 250) {
      globalHR = hr;
    }
  } else {
    //Serial.println("Reconnect failed");
  }
}

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

/* 
 *
 *
 *
 */
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

  //Serial.println("All files deleted.");
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
  collectHR();

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
    line += String(globalHR);
    line += "\n";

    globalFile.print(line);
    globalFile.flush();

    Serial.println("Wrote: " + line);
    listFiles();
  }
}
