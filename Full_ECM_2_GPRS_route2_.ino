#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include <HardwareSerial.h>
#include <SD.h>
#include <SPI.h>
#include "max6675.h"
#include <Adafruit_ADXL345_U.h>

// ---------------- ThingSpeak Config ----------------
unsigned long myChannelNumber = 3063242;
const char* myWriteAPIKey = "AHFKBWTAGI8AW5H3";
const char* thingspeakServer = "api.thingspeak.com";

// ---------------- SIM800L Config ----------------
HardwareSerial sim800(2);
const char* operatorPhone = "+2348169612795";
const char* apn = "internet.ng.airtel.comt";  // CHANGE THIS to your network APN
const char* apnUser = "";         // Usually empty for most networks
const char* apnPass = "";         // Usually empty for most networks

bool smsAlertSent = false;
unsigned long criticalStartTime = 0;
bool inCriticalState = false;
String criticalReason = "";
bool gprsConnected = false;

// ---------------- SD Card Config ----------------
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18
bool sdCardReady = false;
String currentLogFile = "";
int pendingUploads = 0;

// ---------------- Hardware Setup ----------------
const int RS = 13, EN = 14, D4 = 27, D5 = 26, D6 = 25, D7 = 33;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

int thermoSO = 19, thermoCS = 15, thermoSCK = 18;
MAX6675 thermocouple(thermoSCK, thermoCS, thermoSO);

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// ---------------- Vibration Baseline ----------------
const float BASELINE_X = 0.08;
const float BASELINE_Y = 0.00;
const float BASELINE_Z = 11.18;

// ---------------- Sensor Status ----------------
struct SensorStatus {
  bool adxl345_working = true;
  int consecutive_failures = 0;
  unsigned long last_good_reading = 0;
  float last_good_vibration = 0.0;
  float last_good_x = 0.0, last_good_y = 0.0, last_good_z = 0.0;
};

SensorStatus sensorStatus;

// ---------------- Data Buffers ----------------
const int VIBRATION_SAMPLES = 100;
float vibrationBuffer[VIBRATION_SAMPLES];
int vibrationBufferIndex = 0;

// ---------------- Analysis Variables ----------------
float vibrationRMS = 0.0;
int systemStatus = 0; // 0=Good, 1=Not-Good, 2=Critical
unsigned long dataPointCounter = 0;

// ---------------- Thresholds ----------------
const float TEMP_THRESHOLD_CRITICAL = 70.0;
const float TEMP_THRESHOLD_WARNING = 60.0;
const float VIBRATION_RMS_CRITICAL = 0.8;
const float VIBRATION_RMS_WARNING = 0.5;

// ---------------- Timing ----------------
unsigned long lastSDWrite = 0;
unsigned long lastCloudUpload = 0;
unsigned long lastRMSCalculation = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastStatusCheck = 0;
unsigned long lastRetryPending = 0;
unsigned long lastFileCleanup = 0;

const unsigned long SD_WRITE_INTERVAL = 1000;          // 1 second
const unsigned long CLOUD_UPLOAD_INTERVAL = 60000;     // 60 seconds
const unsigned long RMS_CALCULATION_INTERVAL = 3000;   // 3 seconds
const unsigned long LCD_UPDATE_INTERVAL = 500;         // 0.5 second
const unsigned long STATUS_CHECK_INTERVAL = 3000;      // 3 seconds
const unsigned long RETRY_PENDING_INTERVAL = 300000;   // 5 minutes
const unsigned long FILE_CLEANUP_INTERVAL = 86400000;  // 24 hours
const unsigned long CRITICAL_SMS_DELAY = 5000;         // 5 seconds

const int DATA_RETENTION_DAYS = 30;

// ---------------- Helper Functions ----------------
String getDateStamp() {
  // Simple date from uptime (you can add RTC later)
  unsigned long days = millis() / 86400000;
  return "DAY" + String(days);
}

String getTimeStamp() {
  unsigned long totalSeconds = millis() / 1000;
  unsigned long hours = (totalSeconds / 3600) % 24;
  unsigned long minutes = (totalSeconds / 60) % 60;
  unsigned long seconds = totalSeconds % 60;
  
  char timeStr[9];
  sprintf(timeStr, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(timeStr);
}

float calculateRMS(float* buffer, int length) {
  float sum = 0;
  int validSamples = 0;
  for (int i = 0; i < length; i++) {
    if (buffer[i] != 0.0) {
      sum += buffer[i] * buffer[i];
      validSamples++;
    }
  }
  if (validSamples == 0) return 0.0;
  return sqrt(sum / validSamples);
}

// ---------------- SD Card Functions ----------------
bool initSDCard() {
  Serial.println("Initializing SD card...");
  
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    return false;
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.print("SD Card Size: ");
  Serial.print(cardSize);
  Serial.println(" MB");
  
  // Create logs directory if it doesn't exist
  if (!SD.exists("/logs")) {
    SD.mkdir("/logs");
    Serial.println("Created /logs directory");
  }
  
  sdCardReady = true;
  Serial.println("SD Card ready!");
  return true;
}

String getCurrentLogFileName() {
  return "/logs/" + getDateStamp() + ".csv";
}

void writeToSD(float temp, float vibRMS, float vibInst, int status, bool uploaded) {
  if (!sdCardReady) return;
  
  String filename = getCurrentLogFileName();
  bool fileExists = SD.exists(filename);
  
  File dataFile = SD.open(filename, FILE_APPEND);
  if (!dataFile) {
    Serial.println("Error opening log file!");
    return;
  }
  
  // Write header if new file
  if (!fileExists) {
    dataFile.println("timestamp,temp_C,vib_rms_g,vib_inst_g,status,uploaded");
  }
  
  // Write data
  dataFile.print(dataPointCounter++);
  dataFile.print(",");
  dataFile.print(temp, 2);
  dataFile.print(",");
  dataFile.print(vibRMS, 3);
  dataFile.print(",");
  dataFile.print(vibInst, 3);
  dataFile.print(",");
  dataFile.print(status);
  dataFile.print(",");
  dataFile.println(uploaded ? "1" : "0");
  
  dataFile.close();
  
  if (!uploaded) {
    pendingUploads++;
  }
}

void cleanupOldFiles() {
  Serial.println("Cleaning up old log files...");
  
  File root = SD.open("/logs");
  if (!root) return;
  
  File file = root.openNextFile();
  int deletedCount = 0;
  
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      // Simple cleanup: delete if not today's file and more than retention period
      // (Simplified - you can enhance with actual date comparison)
      if (filename != getCurrentLogFileName()) {
        // Check file age by comparing day numbers
        // For now, keep last 30 files (simple approach)
        deletedCount++;
      }
    }
    file = root.openNextFile();
  }
  
  root.close();
  Serial.print("Cleanup complete. Files checked: ");
  Serial.println(deletedCount);
}

// ---------------- ADXL345 Functions ----------------
bool readADXL345Robust(float &vibration_x, float &vibration_y, float &vibration_z, float &vibration_magnitude) {
  sensors_event_t event;
  bool success = accel.getEvent(&event);
  
  if (success) {
    vibration_x = event.acceleration.x - BASELINE_X;
    vibration_y = event.acceleration.y - BASELINE_Y;
    vibration_z = event.acceleration.z - BASELINE_Z;
    vibration_magnitude = sqrt(vibration_x*vibration_x + vibration_y*vibration_y + vibration_z*vibration_z);
    
    sensorStatus.consecutive_failures = 0;
    sensorStatus.adxl345_working = true;
    sensorStatus.last_good_reading = millis();
    sensorStatus.last_good_vibration = vibration_magnitude;
    sensorStatus.last_good_x = vibration_x;
    sensorStatus.last_good_y = vibration_y;
    sensorStatus.last_good_z = vibration_z;
    
    return true;
  } else {
    sensorStatus.consecutive_failures++;
    sensorStatus.adxl345_working = false;
    
    if (sensorStatus.consecutive_failures >= 5) {
      Wire.begin(21, 22);
      delay(100);
      accel.begin();
      delay(100);
      sensorStatus.consecutive_failures = 0;
    }
    
    unsigned long age = millis() - sensorStatus.last_good_reading;
    if (age < 30000) {
      vibration_x = sensorStatus.last_good_x;
      vibration_y = sensorStatus.last_good_y;
      vibration_z = sensorStatus.last_good_z;
      vibration_magnitude = sensorStatus.last_good_vibration;
      return false;
    } else {
      vibration_x = vibration_y = vibration_z = vibration_magnitude = 0.0;
      return false;
    }
  }
}

void updateVibrationBuffer(float vibrationMagnitude) {
  if (vibrationMagnitude >= 0.0) {
    vibrationBuffer[vibrationBufferIndex] = vibrationMagnitude;
    vibrationBufferIndex = (vibrationBufferIndex + 1) % VIBRATION_SAMPLES;
  }
}

void calculateRMSValues() {
  vibrationRMS = calculateRMS(vibrationBuffer, VIBRATION_SAMPLES);
}

// ---------------- Status Functions ----------------
void determineSystemStatus(float temperature, float vibRMS) {
  int previousStatus = systemStatus;
  
  static float tempBuffer = 2.0;
  static float vibBuffer = 0.05;
  
  bool isTempCritical = false;
  bool isVibCritical = false;
  bool isTempWarning = false;
  bool isVibWarning = false;
  
  if (systemStatus == 2 && temperature > (TEMP_THRESHOLD_CRITICAL - tempBuffer)) {
    isTempCritical = true;
  } else if (temperature > TEMP_THRESHOLD_CRITICAL) {
    isTempCritical = true;
  } else if (systemStatus == 1 && temperature > (TEMP_THRESHOLD_WARNING - tempBuffer)) {
    isTempWarning = true;
  } else if (temperature > TEMP_THRESHOLD_WARNING) {
    isTempWarning = true;
  }
  
  if (systemStatus == 2 && vibRMS > (VIBRATION_RMS_CRITICAL - vibBuffer)) {
    isVibCritical = true;
  } else if (vibRMS > VIBRATION_RMS_CRITICAL) {
    isVibCritical = true;
  } else if (systemStatus == 1 && vibRMS > (VIBRATION_RMS_WARNING - vibBuffer)) {
    isVibWarning = true;
  } else if (vibRMS > VIBRATION_RMS_WARNING) {
    isVibWarning = true;
  }
  
  if (isTempCritical || isVibCritical) {
    systemStatus = 2;
    if (isTempCritical && isVibCritical) {
      criticalReason = "CT+CV";
    } else if (isTempCritical) {
      criticalReason = "CT";
    } else {
      criticalReason = "CV";
    }
  } else if (isTempWarning || isVibWarning) {
    systemStatus = 1;
    criticalReason = "";
  } else {
    systemStatus = 0;
    criticalReason = "";
  }
  
  if (systemStatus == 2 && previousStatus != 2) {
    inCriticalState = true;
    criticalStartTime = millis();
    smsAlertSent = false;
    Serial.println("=== CRITICAL STATE ===");
    Serial.print("Reason: "); Serial.println(criticalReason);
  } else if (systemStatus == 2) {
    inCriticalState = true;
  } else {
    if (inCriticalState) {
      Serial.println("=== EXITED CRITICAL ===");
    }
    inCriticalState = false;
    smsAlertSent = false;
  }
}

// ---------------- SIM800L GPRS Functions ----------------
void sendATCommand(String command, int timeout) {
  Serial.print(">> ");
  Serial.println(command);
  
  sim800.println(command);
  
  long int time = millis();
  while ((time + timeout) > millis()) {
    while (sim800.available()) {
      Serial.write(sim800.read());
    }
  }
}

bool initGPRS() {
  Serial.println("=== Initializing GPRS ===");
  
  sendATCommand("AT", 1000);
  sendATCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", 2000);
  
  String apnCmd = "AT+SAPBR=3,1,\"APN\",\"" + String(apn) + "\"";
  sendATCommand(apnCmd, 2000);
  
  if (String(apnUser) != "") {
    String userCmd = "AT+SAPBR=3,1,\"USER\",\"" + String(apnUser) + "\"";
    sendATCommand(userCmd, 2000);
  }
  
  if (String(apnPass) != "") {
    String passCmd = "AT+SAPBR=3,1,\"PWD\",\"" + String(apnPass) + "\"";
    sendATCommand(passCmd, 2000);
  }
  
  sendATCommand("AT+SAPBR=1,1", 5000);
  sendATCommand("AT+SAPBR=2,1", 2000);
  
  gprsConnected = true;
  Serial.println("GPRS Connected!");
  return true;
}

bool uploadToThingSpeak(float temp, float vibRMS, float vibInst, int status) {
  if (!gprsConnected) {
    Serial.println("GPRS not connected, attempting to connect...");
    if (!initGPRS()) {
      return false;
    }
  }
  
  Serial.println("=== Uploading to ThingSpeak ===");
  
  // Initialize HTTP
  sendATCommand("AT+HTTPINIT", 2000);
  sendATCommand("AT+HTTPPARA=\"CID\",1", 2000);
  
  // Build URL
  String url = "http://" + String(thingspeakServer) + "/update?api_key=" + String(myWriteAPIKey);
  url += "&field1=" + String(vibRMS, 3);
  url += "&field2=" + String(vibInst, 3);
  url += "&field5=" + String(temp, 2);
  url += "&field6=" + String(status);
  
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendATCommand(urlCmd, 2000);
  
  // Send HTTP GET request
  sim800.println("AT+HTTPACTION=0");
  delay(5000);
  
  // Check response
  String response = "";
  unsigned long timeout = millis() + 5000;
  bool success = false;
  
  while (millis() < timeout) {
    if (sim800.available()) {
      char c = sim800.read();
      response += c;
      Serial.write(c);
      
      if (response.indexOf("+HTTPACTION: 0,200") >= 0) {
        success = true;
        break;
      }
    }
  }
  
  // Cleanup
  sendATCommand("AT+HTTPTERM", 2000);
  
  if (success) {
    Serial.println("\nThingSpeak upload SUCCESS!");
    return true;
  } else {
    Serial.println("\nThingSpeak upload FAILED");
    return false;
  }
}

// ---------------- SMS Functions ----------------
void sendSMS(String message) {
  Serial.println("=== SENDING SMS ===");
  
  while (sim800.available()) sim800.read();
  
  sim800.println("AT+CMGF=1");
  delay(1000);
  
  sim800.print("AT+CMGS=\"");
  sim800.print(operatorPhone);
  sim800.println("\"");
  delay(1000);
  
  unsigned long timeout = millis() + 5000;
  bool gotPrompt = false;
  while (millis() < timeout && !gotPrompt) {
    if (sim800.available()) {
      char c = sim800.read();
      Serial.write(c);
      if (c == '>') gotPrompt = true;
    }
  }
  
  if (!gotPrompt) {
    Serial.println("\nSMS failed - no prompt");
    return;
  }
  
  sim800.print(message);
  delay(500);
  sim800.write(26);
  
  timeout = millis() + 10000;
  String response = "";
  while (millis() < timeout) {
    if (sim800.available()) {
      char c = sim800.read();
      Serial.write(c);
      response += c;
      if (response.indexOf("OK") >= 0) {
        smsAlertSent = true;
        Serial.println("\nSMS sent!");
        return;
      }
    }
  }
}

void checkAndSendCriticalAlert(float temperature, float vibration) {
  if (inCriticalState && !smsAlertSent) {
    unsigned long criticalDuration = millis() - criticalStartTime;
    
    if (criticalDuration >= CRITICAL_SMS_DELAY) {
      String alertMessage = "EQUIPMENT ALERT!\nStatus: CRITICAL (";
      alertMessage += criticalReason;
      alertMessage += ")\n";
      
      if (criticalReason.indexOf("CT") >= 0) {
        alertMessage += "Temp: ";
        alertMessage += String(temperature, 1);
        alertMessage += "C\n";
      }
      
      if (criticalReason.indexOf("CV") >= 0) {
        alertMessage += "Vib: ";
        alertMessage += String(vibration, 2);
        alertMessage += "g\n";
      }
      
      alertMessage += "Check equipment now!";
      sendSMS(alertMessage);
    }
  }
}

// ---------------- LCD Functions ----------------
void updateLCD(float temperature, float vibration) {
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temperature, 0);
  lcd.print("C ");
  
  lcd.setCursor(8, 0);
  if (!sensorStatus.adxl345_working) {
    lcd.print("SENS-E");
  } else {
    switch(systemStatus) {
      case 0: lcd.print("OK    "); break;
      case 1: lcd.print("WARN  "); break;
      case 2: lcd.print(criticalReason); lcd.print("  "); break;
    }
  }
  
  lcd.setCursor(0, 1);
  lcd.print("V:");
  lcd.print(vibration, 2);
  lcd.print("g");
  
  if (sdCardReady) {
    lcd.setCursor(11, 1);
    lcd.print("SD");
  }
  
  if (gprsConnected) {
    lcd.setCursor(14, 1);
    lcd.print("G");
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=================================");
  Serial.println("Equipment Monitoring System v2.0");
  Serial.println("SD Card + GPRS Cloud Backup");
  Serial.println("=================================\n");
  
  Serial.println("[1/8] Initializing buffers...");
  for (int i = 0; i < VIBRATION_SAMPLES; i++) vibrationBuffer[i] = 0;
  Serial.println("      Buffers OK");

  Serial.println("[2/8] Initializing LCD...");
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("System Starting");
  Serial.println("      LCD OK");
  delay(1000);

  Serial.println("[3/8] Initializing I2C...");
  Wire.begin(21, 22);
  Wire.setClock(50000);
  Wire.setTimeout(5000);
  Serial.println("      I2C OK");
  
  // SD Card
  Serial.println("[4/8] Initializing SD Card...");
  lcd.clear();
  lcd.print("SD Card...");
  
  if (initSDCard()) {
    Serial.println("      SD Card OK");
    lcd.setCursor(0, 1);
    lcd.print("SD: OK");
  } else {
    Serial.println("      SD Card FAILED (continuing anyway)");
    lcd.setCursor(0, 1);
    lcd.print("SD: FAILED");
    sdCardReady = false;
  }
  delay(2000);

  // ADXL345
  Serial.println("[5/8] Initializing ADXL345...");
  lcd.clear();
  lcd.print("ADXL345...");
  
  if (!accel.begin()) {
    Serial.println("      ADXL345 FAILED (will retry during operation)");
    sensorStatus.adxl345_working = false;
    lcd.setCursor(0, 1);
    lcd.print("ADXL: ERROR");
  } else {
    Serial.println("      ADXL345 OK");
    accel.setRange(ADXL345_RANGE_16_G);
    sensorStatus.adxl345_working = true;
    lcd.setCursor(0, 1);
    lcd.print("ADXL: OK");
  }
  delay(2000);
  
  // SIM800L
  Serial.println("[6/8] Initializing SIM800L...");
  lcd.clear();
  lcd.print("SIM800L...");
  
  sim800.begin(9600, SERIAL_8N1, 17, 16);
  delay(3000);
  
  Serial.println("      Testing AT command...");
  sendATCommand("AT", 1000);
  
  Serial.println("      Setting SMS mode...");
  sendATCommand("AT+CMGF=1", 1000);
  
  Serial.println("      SIM800L OK");
  lcd.setCursor(0, 1);
  lcd.print("SIM: OK");
  delay(2000);
  
  // GPRS
  Serial.println("[7/8] Connecting to GPRS...");
  Serial.println("      This may take 10-30 seconds...");
  lcd.clear();
  lcd.print("Connecting GPRS");
  
  if (initGPRS()) {
    Serial.println("      GPRS Connected!");
    lcd.setCursor(0, 1);
    lcd.print("GPRS: Connected");
  } else {
    Serial.println("      GPRS connection failed (will retry later)");
    lcd.setCursor(0, 1);
    lcd.print("GPRS: FAILED");
    gprsConnected = false;
  }
  delay(2000);
  
  Serial.println("[8/8] Startup complete!");
  lcd.clear();
  lcd.print("System Ready!");
  Serial.println("\n=== SYSTEM READY ===\n");
  Serial.println("Monitoring started...\n");
  delay(1500);
}

// ---------------- Main Loop ----------------
void loop() {
  unsigned long currentTime = millis();
  
  // Read sensors
  float temperature = thermocouple.readCelsius();
  float vib_x, vib_y, vib_z, vibration_magnitude;
  readADXL345Robust(vib_x, vib_y, vib_z, vibration_magnitude);
  updateVibrationBuffer(vibration_magnitude);
  
  // Calculate RMS every 3 seconds
  if (currentTime - lastRMSCalculation >= RMS_CALCULATION_INTERVAL) {
    calculateRMSValues();
    lastRMSCalculation = currentTime;
  }
  
  // Status check every 3 seconds
  if (currentTime - lastStatusCheck >= STATUS_CHECK_INTERVAL) {
    determineSystemStatus(temperature, vibrationRMS);
    lastStatusCheck = currentTime;
  }
  
  // Write to SD every 1 second
  if (currentTime - lastSDWrite >= SD_WRITE_INTERVAL) {
    writeToSD(temperature, vibrationRMS, vibration_magnitude, systemStatus, false);
    lastSDWrite = currentTime;
  }
  
  // Upload to cloud every 60 seconds
  if (currentTime - lastCloudUpload >= CLOUD_UPLOAD_INTERVAL) {
    bool uploadSuccess = uploadToThingSpeak(temperature, vibrationRMS, vibration_magnitude, systemStatus);
    if (uploadSuccess && sdCardReady) {
      // Mark last entry as uploaded
      // (Simplified - in production you'd update the CSV file)
      if (pendingUploads > 0) pendingUploads--;
    }
    lastCloudUpload = currentTime;
  }
  
  // Check critical alerts
  checkAndSendCriticalAlert(temperature, vibrationRMS);
  
  // Update LCD every 0.5 seconds
  if (currentTime - lastLCDUpdate >= LCD_UPDATE_INTERVAL) {
    updateLCD(temperature, vibration_magnitude);
    lastLCDUpdate = currentTime;
  }
  
  // Cleanup old files every 24 hours
  if (currentTime - lastFileCleanup >= FILE_CLEANUP_INTERVAL) {
    cleanupOldFiles();
    lastFileCleanup = currentTime;
  }
  
  // Serial output
  Serial.print(getTimeStamp());
  Serial.print(" T:");
  Serial.print(temperature, 1);
  Serial.print("C V:");
  Serial.print(vibration_magnitude, 3);
  Serial.print("g VRMS:");
  Serial.print(vibrationRMS, 3);
  Serial.print("g S:");
  Serial.print(systemStatus);
  Serial.print(" SD:");
  Serial.print(sdCardReady ? "OK" : "X");
  Serial.print(" GPRS:");
  Serial.println(gprsConnected ? "OK" : "X");
  
  delay(1000);
}