/*
 * ESP32 Authentication Device
 * Handles fingerprint (R307) and RFID (RC522) authentication
 * Communicates with Firebase for worker verification and logging
 */

#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_Fingerprint.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

// WiFi credentials
#define WIFI_SSID "iPhone"
#define WIFI_PASSWORD "12345678"

// Firebase credentials
#define FIREBASE_HOST "worknest01-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "AIzaSyBIE39DJ_fZNnS5mucUDtCBw-qrXevSCOI"

// RFID pins
#define SS_PIN 5
#define RST_PIN 27

// Status LED pins
#define LED_SUCCESS 12
#define LED_ERROR 14
#define LED_PROCESSING 13

// Initialize Firebase
FirebaseData firebaseData;
FirebaseJson json;

// Initialize RFID
MFRC522 rfid(SS_PIN, RST_PIN);

// Initialize Fingerprint sensor on Serial2 (RX:16, TX:17)
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Function prototypes
void connectWiFi();
String readRFID();
int checkFingerprint();
bool verifyWorker(String rfidTag, int fingerprintID);
bool clockInOut(String workerID);
void sendToFirebase(String path, FirebaseJson &data);
String getWorkerIDFromRFID(String rfidTag);
String getWorkerIDFromFingerprint(int fingerprintID);
String getLastAction(String workerID);

void setup() {
  Serial.begin(115200);
  
  // Initialize status LEDs
  pinMode(LED_SUCCESS, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  pinMode(LED_PROCESSING, OUTPUT);
  
  // Turn all LEDs off
  digitalWrite(LED_SUCCESS, LOW);
  digitalWrite(LED_ERROR, LOW);
  digitalWrite(LED_PROCESSING, LOW);
  
  // Connect to WiFi
  connectWiFi();
  
  // Initialize Firebase
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);
  
  // Initialize RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID reader initialized");
  
  // Initialize Fingerprint sensor
  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor initialized");
  } else {
    Serial.println("Fingerprint sensor not found");
    digitalWrite(LED_ERROR, HIGH);
    delay(3000);
    digitalWrite(LED_ERROR, LOW);
  }
  
  Serial.println("ESP32 Authentication Device Ready");
}

void loop() {
  // Check for RFID card
  String rfidTag = readRFID();
  
  if (rfidTag != "") {
    digitalWrite(LED_PROCESSING, HIGH);
    Serial.println("RFID detected: " + rfidTag);
    
    // Get worker ID from RFID
    String workerID = getWorkerIDFromRFID(rfidTag);
    
    if (workerID != "") {
      // RFID verification successful
      Serial.println("Worker ID: " + workerID);
      
      // Clock in/out the worker
      if (clockInOut(workerID)) {
        digitalWrite(LED_SUCCESS, HIGH);
        delay(2000);
        digitalWrite(LED_SUCCESS, LOW);
      } else {
        digitalWrite(LED_ERROR, HIGH);
        delay(2000);
        digitalWrite(LED_ERROR, LOW);
      }
    } else {
      // RFID not recognized
      Serial.println("RFID not recognized");
      digitalWrite(LED_ERROR, HIGH);
      delay(2000);
      digitalWrite(LED_ERROR, LOW);
    }
    
    digitalWrite(LED_PROCESSING, LOW);
  }
  
  // Check for fingerprint
  int fingerprintID = checkFingerprint();
  
  if (fingerprintID >= 0) {
    digitalWrite(LED_PROCESSING, HIGH);
    Serial.println("Fingerprint detected: " + String(fingerprintID));
    
    // Get worker ID from fingerprint
    String workerID = getWorkerIDFromFingerprint(fingerprintID);
    
    if (workerID != "") {
      // Fingerprint verification successful
      Serial.println("Worker ID: " + workerID);
      
      // Clock in/out the worker
      if (clockInOut(workerID)) {
        digitalWrite(LED_SUCCESS, HIGH);
        delay(2000);
        digitalWrite(LED_SUCCESS, LOW);
      } else {
        digitalWrite(LED_ERROR, HIGH);
        delay(2000);
        digitalWrite(LED_ERROR, LOW);
      }
    } else {
      // Fingerprint not recognized
      Serial.println("Fingerprint not recognized");
      digitalWrite(LED_ERROR, HIGH);
      delay(2000);
      digitalWrite(LED_ERROR, LOW);
    }
    
    digitalWrite(LED_PROCESSING, LOW);
  }
  
  delay(100);
}

// Connect to WiFi
void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PROCESSING, HIGH);
    delay(500);
    digitalWrite(LED_PROCESSING, LOW);
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: " + WiFi.localIP().toString());
  
  // Success indication
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_SUCCESS, HIGH);
    delay(200);
    digitalWrite(LED_SUCCESS, LOW);
    delay(200);
  }
}

// Read RFID tag
String readRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return "";
  }
  
  String tag = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    tag += (rfid.uid.uidByte[i] < 0x10 ? "0" : "") + String(rfid.uid.uidByte[i], HEX);
  }
  tag.toUpperCase();
  
  // Halt PICC and stop encryption
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  return tag;
}

// Check for fingerprint
int checkFingerprint() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    return -1;
  }
  
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    return -1;
  }
  
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) {
    return -1;
  }
  
  return finger.fingerID;
}

// Get worker ID from RFID tag
String getWorkerIDFromRFID(String rfidTag) {
  String workerID = "";
  
  // Search through all admin nodes
  if (Firebase.getJSON(firebaseData, "/admin")) {
    FirebaseJson &adminJson = firebaseData.jsonObject();
    size_t adminCount = adminJson.iteratorBegin();
    String adminKey, adminVal;
    int adminType = 0;
    
    for (size_t i = 0; i < adminCount; i++) {
      adminJson.iteratorGet(i, adminType, adminKey, adminVal);
      
      // For each admin, search through workers
      if (Firebase.getJSON(firebaseData, "/admin/" + adminKey + "/workers")) {
        FirebaseJson &workersJson = firebaseData.jsonObject();
        size_t workerCount = workersJson.iteratorBegin();
        String workerKey, workerVal;
        int workerType = 0;
        
        for (size_t j = 0; j < workerCount; j++) {
          workersJson.iteratorGet(j, workerType, workerKey, workerVal);
          
          // Check if this worker has the matching RFID
          if (Firebase.getString(firebaseData, "/admin/" + adminKey + "/workers/" + workerKey + "/rfid_id")) {
            String storedRFID = firebaseData.stringData();
            
            if (storedRFID == rfidTag) {
              workerID = workerKey;
              break;
            }
          }
        }
        
        workersJson.iteratorEnd();
      }
      
      if (workerID != "") {
        break;
      }
    }
    
    adminJson.iteratorEnd();
  }
  
  return workerID;
}

// Get worker ID from fingerprint ID
String getWorkerIDFromFingerprint(int fingerprintID) {
  String workerID = "";
  
  // Search through all admin nodes
  if (Firebase.getJSON(firebaseData, "/admin")) {
    FirebaseJson &adminJson = firebaseData.jsonObject();
    size_t adminCount = adminJson.iteratorBegin();
    String adminKey, adminVal;
    int adminType = 0;
    
    for (size_t i = 0; i < adminCount; i++) {
      adminJson.iteratorGet(i, adminType, adminKey, adminVal);
      
      // For each admin, search through workers
      if (Firebase.getJSON(firebaseData, "/admin/" + adminKey + "/workers")) {
        FirebaseJson &workersJson = firebaseData.jsonObject();
        size_t workerCount = workersJson.iteratorBegin();
        String workerKey, workerVal;
        int workerType = 0;
        
        for (size_t j = 0; j < workerCount; j++) {
          workersJson.iteratorGet(j, workerType, workerKey, workerVal);
          
          // Check if this worker has the matching fingerprint ID
          if (Firebase.getString(firebaseData, "/admin/" + adminKey + "/workers/" + workerKey + "/fingerprint_id")) {
            String storedFingerprintID = firebaseData.stringData();
            
            if (storedFingerprintID == String(fingerprintID)) {
              workerID = workerKey;
              break;
            }
          }
        }
        
        workersJson.iteratorEnd();
      }
      
      if (workerID != "") {
        break;
      }
    }
    
    adminJson.iteratorEnd();
  }
  
  return workerID;
}

// Get the last action (clock in/out) for a worker
String getLastAction(String workerID) {
  String lastAction = "out"; // Default to "out" if no record found
  String currentDate = getCurrentDate();
  
  // Search through all admin nodes to find this worker
  if (Firebase.getJSON(firebaseData, "/admin")) {
    FirebaseJson &adminJson = firebaseData.jsonObject();
    size_t adminCount = adminJson.iteratorBegin();
    String adminKey, adminVal;
    int adminType = 0;
    
    for (size_t i = 0; i < adminCount; i++) {
      adminJson.iteratorGet(i, adminType, adminKey, adminVal);
      
      // Check if this admin has the worker
      if (Firebase.getJSON(firebaseData, "/admin/" + adminKey + "/workers/" + workerID)) {
        // Check if there's a record for today
        if (Firebase.getString(firebaseData, "/admin/" + adminKey + "/workers/" + workerID + "/" + currentDate + "/clock_in")) {
          // There's a clock-in record for today
          String clockIn = firebaseData.stringData();
          
          if (Firebase.getString(firebaseData, "/admin/" + adminKey + "/workers/" + workerID + "/" + currentDate + "/clock_out")) {
            // There's also a clock-out record
            String clockOut = firebaseData.stringData();
            
            if (clockOut != "") {
              // If clock-out time exists and is not empty, last action was clock-out
              lastAction = "out";
            } else {
              // If clock-out time doesn't exist or is empty, last action was clock-in
              lastAction = "in";
            }
          } else {
            // No clock-out record, so last action was clock-in
            lastAction = "in";
          }
          
          break;
        }
      }
    }
    
    adminJson.iteratorEnd();
  }
  
  return lastAction;
}

// Clock in or out a worker
bool clockInOut(String workerID) {
  bool success = false;
  String currentDate = getCurrentDate();
  String currentTime = getCurrentTime();
  String lastAction = getLastAction(workerID);
  
  // Search through all admin nodes to find this worker
  if (Firebase.getJSON(firebaseData, "/admin")) {
    FirebaseJson &adminJson = firebaseData.jsonObject();
    size_t adminCount = adminJson.iteratorBegin();
    String adminKey, adminVal;
    int adminType = 0;
    
    for (size_t i = 0; i < adminCount; i++) {
      adminJson.iteratorGet(i, adminType, adminKey, adminVal);
      
      // Check if this admin has the worker
      if (Firebase.getJSON(firebaseData, "/admin/" + adminKey + "/workers/" + workerID)) {
        FirebaseJson updateJson;
        
        if (lastAction == "out") {
          // Last action was clock-out, so clock-in now
          updateJson.set(currentDate + "/clock_in", currentTime);
          
          // Also activate GPS tracking for this worker
          if (Firebase.getString(firebaseData, "/admin/" + adminKey + "/workers/" + workerID + "/gps_device_id")) {
            String gpsDeviceID = firebaseData.stringData();
            
            if (gpsDeviceID != "") {
              FirebaseJson gpsJson;
              gpsJson.set("active", true);
              Firebase.updateNode(firebaseData, "/gps_devices/" + gpsDeviceID, gpsJson);
            }
          }
        } else {
          // Last action was clock-in, so clock-out now
          updateJson.set(currentDate + "/clock_out", currentTime);
          
          // Also deactivate GPS tracking for this worker
          if (Firebase.getString(firebaseData, "/admin/" + adminKey + "/workers/" + workerID + "/gps_device_id")) {
            String gpsDeviceID = firebaseData.stringData();
            
            if (gpsDeviceID != "") {
              FirebaseJson gpsJson;
              gpsJson.set("active", false);
              Firebase.updateNode(firebaseData, "/gps_devices/" + gpsDeviceID, gpsJson);
            }
          }
        }
        
        // Update Firebase
        if (Firebase.updateNode(firebaseData, "/admin/" + adminKey + "/workers/" + workerID, updateJson)) {
          Serial.println("Clock " + (lastAction == "out" ? "in" : "out") + " successful");
          success = true;
        } else {
          Serial.println("Firebase update failed: " + firebaseData.errorReason());
        }
        
        break;
      }
    }
    
    adminJson.iteratorEnd();
  }
  
  return success;
}

// Get current date in YYYY-MM-DD format
String getCurrentDate() {
  // In a real implementation, you would use NTP to get the current date
  // For simplicity, we'll use a placeholder
  return "2025-03-01"; // Replace with actual NTP code
}

// Get current time in HH:MM:SS format
String getCurrentTime() {
  // In a real implementation, you would use NTP to get the current time
  // For simplicity, we'll use a placeholder
  return "00:50:08"; // Replace with actual NTP code
}
