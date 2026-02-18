/*
 * CAN Data Logger for ESP32-C6 Pico
 * Optimized for data logging with .NXT file format (compatible with decoder)
 * Hardware: ESP32-C6 Pico (Waveshare), TJA1050 CAN Transceiver, DS3231 RTC, SD Card Module
 * 
 * Features:
 * - Logs data from at least 10 CAN IDs
 * - Encrypted data storage on SD card (.NXT format with stream-cipher encryption)
 * - RTC for accurate timestamps
 * - WiFi Access Point + Station mode
 * - Web server for data file access with live data
 * - Neopixel LED status indicators
 * - Optimized data logging with batch processing
 * 
 * File Format: .NXT (NXTLOG header + encrypted CSV payload)
 */

 #include <sys/time.h>
 #include <time.h>
 #include <SD.h>
 #include <SPI.h>
 #include <Wire.h>
 #include <RTClib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
 #include <esp_system.h>
 #include <esp_random.h>
 #include <cstring>
 #include <math.h>
 #include <HardwareSerial.h>
 #include <Adafruit_NeoPixel.h>
 #include "driver/twai.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 
// Shared I2C bus for RTC + IMU (use default Wire instance)
TwoWire &WireRTC = Wire;
 
 // Pin definitions for ESP32-C6 (match hardware wiring)
 #define CAN_TX_PIN     17
 #define CAN_RX_PIN     16
 
 // SD Card pins for ESP32-C6
 #define SD_CS_PIN      15
 #define SD_MOSI_PIN    20
 #define SD_MISO_PIN    19
 #define SD_SCK_PIN     18
 #define SD_CARD_SPEED  4000000  // 4MHz SPI speed (optimized for high-speed logging)
 
 // RTC I2C bus
 #define RTC_SDA_PIN    22
 #define RTC_SCL_PIN    23
 
 // Neopixel LED
 #define NEOPIXEL_PIN   8
 #define NEOPIXEL_COUNT 1
 // Initialize in setup() to avoid global constructor issues
 Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
 
// SD Card settings - No enforced file size limit (log until stop/power-off)
#define MAX_FILE_SIZE  0  // unused (kept for compatibility)
#define LOG_FILE_PREFIX "CAN_LOG_"
#define FLUSH_INTERVAL 20  // Flush every 20 frames for better performance with 1000 samples
// Note: System optimized for high-throughput logging of 1000+ samples seamlessly

// CAN bus speed (kbps) - update to match your bus
#define CAN_SPEED_KBPS 500

// CAN RX queue size (increase to avoid dropping frames when many IDs are present)
#define CAN_RX_QUEUE_LEN 256

// Keep CAN rate constant at 500 kbps (no auto-baud)
int currentCanSpeedKbps = CAN_SPEED_KBPS;
 
// WiFi Configuration
#define ENABLE_WIFI     1
#define ENABLE_OTA      1

#define OTA_HOSTNAME    "can-data-logger"
#define OTA_PORT        3232
 
 // WiFi Access Point Configuration
 #define WIFI_AP_SSID    "CAN_Data_Logger"
 #define WIFI_AP_PASS    "CANDataLogger123"
 #define WIFI_AP_IP      IPAddress(192, 168, 10, 1)
 #define WIFI_AP_GATEWAY IPAddress(192, 168, 10, 1)
 #define WIFI_AP_SUBNET  IPAddress(255, 255, 255, 0)
 
 // WiFi Station Configuration
 #define WIFI_STA_SSID   "AkashGanga"
 #define WIFI_STA_PASS   "Naxatra2025"
 
 // NTP configuration
 #define NTP_SERVER      "pool.ntp.org"
 #define GMT_OFFSET_SEC  19800  // IST = +5:30 = 19800 seconds
 #define DST_OFFSET_SEC  0
 
 // Encrypted log configuration (NXTLOG stream cipher)
 #define NXT_LOG_SIGNATURE "NXTLOG"
 #define NXT_LOG_VERSION   1
 #define NXT_HEADER_SIZE   16
 const uint8_t ENCRYPTION_KEY[16] = {
     0x3A, 0x7C, 0xB5, 0x19,
     0xE4, 0x58, 0xC1, 0x0D,
     0x92, 0xAF, 0x63, 0x27,
     0xFE, 0x34, 0x88, 0x4B
 };
 uint32_t currentFileNonce = 0;
 uint32_t currentCipherState = 0;
 
 // Live data buffer for web server
 #define LIVE_BUFFER_SIZE 200
 #define LIVE_DEFAULT_LIMIT 50
 
 struct LiveFrame {
     uint32_t seq;
     time_t unixTime;
     uint32_t micros;
     char timeText[24];
     uint32_t identifier;
     bool extended;
     bool rtr;
     uint8_t dlc;
     uint8_t data[8];
 };
 
LiveFrame liveFrameBuffer[LIVE_BUFFER_SIZE];
uint32_t liveFrameCounter = 0;
 
 // Tracked CAN IDs - 10 IDs minimum
 const uint32_t trackedCANIDs[] = {
     0x100, 0x101, 0x102, 0x103, 0x104,
     0x105, 0x106, 0x107, 0x108, 0x109
 };
 const uint8_t numTrackedIDs = sizeof(trackedCANIDs) / sizeof(trackedCANIDs[0]);
 
 // CAN Frame structure
 struct CanFrame {
     uint32_t identifier;
     bool extd;
     bool rtr;
     uint8_t data_length_code;
     uint8_t data[8];
 };
 
 // RTC object
 RTC_DS3231 rtc;
bool rtcAvailable = false;
bool i2cReady = false;
 
 // CAN state
 bool canInitialized = false;
 
 // Web Server
 #if ENABLE_WIFI
 WebServer server(80);
 #endif
 
 // System State Variables
 File logFile;
 String currentLogFileName;
 unsigned long currentFileSize = 0;
 bool sdCardReady = false;
 unsigned long messageCount = 0;
unsigned long bootMillis;
time_t baseTime = 0;
uint16_t frameSequence = 0;
uint8_t flushCounter = 0;
bool ntpSynced = false;
unsigned long lastNtpSyncMs = 0;
#define NTP_SYNC_INTERVAL_MS 600000UL  // 10 minutes

// ==================== IMU (ADXL345) ====================
// Hardware notes:
// - ADXL345 shares I2C pins with RTC (SDA=GPIO22, SCL=GPIO23)
// - We use WireRTC for both devices to avoid bus conflicts.
#define ADXL345_ADDR 0x53
bool imuAvailable = false;
bool imuHasData = false;
unsigned long lastImuReadMs = 0;
unsigned long lastImuReadUs = 0;
#define IMU_SAMPLE_INTERVAL_US 1000  // 1 kHz sampling
// ADXL345 FULL_RES scale ~3.9 mg/LSB at +/-2g
#define ADXL_G_PER_LSB 0.0039f
#define ADXL_G_TO_MS2  9.80665f
// Latest filtered linear acceleration (m/s^2)
float linearAccelX = 0.0f;
float linearAccelY = 0.0f;
float linearAccelZ = 0.0f;
float gravityAccel = 0.0f;
float imuScale = 1.0f;
// IMU calibration offsets (raw counts)
float imuOffsetX = 0.0f;
float imuOffsetY = 0.0f;
float imuOffsetZ = 0.0f;
bool imuCalibrated = false;
#define IMU_CALIBRATION_SAMPLES 100

// ==================== GPS (NEO-M8N) ====================
#define GPS_RX_PIN 4
#define GPS_TX_PIN 5
#define GPS_BAUD   9600
HardwareSerial GPSSerial(1);
bool gpsInitialized = false;
bool gpsHasFix = false;
float gpsLat = 0.0f;
float gpsLon = 0.0f;
float gpsAlt = 0.0f;
float gpsSpeed = 0.0f;  // km/h
float gpsCourse = 0.0f;
int gpsSats = 0;
float gpsHdop = 0.0f;
char gpsTimeText[12] = "0";
unsigned long gpsLastFixMs = 0;
#define GPS_FIX_TIMEOUT_MS 5000
 
 
// LED Status Colors
#define LED_RED       pixels.Color(255, 0, 0)        // Error states
#define LED_BLUE      pixels.Color(0, 0, 255)        // Initializing
#define LED_GREEN     pixels.Color(0, 255, 0)         // Ready/Connected
#define LED_YELLOW    pixels.Color(255, 255, 0)      // CAN Bus Initializing
#define LED_CYAN      pixels.Color(0, 255, 255)      // SD Card Setup
#define LED_MAGENTA   pixels.Color(255, 0, 255)      // Data Transfer Active
#define LED_ORANGE    pixels.Color(255, 165, 0)      // Data Transfer Stopped
#define LED_WHITE     pixels.Color(255, 255, 255)   // System Ready
#define LED_OFF       pixels.Color(0, 0, 0)           // Off

uint32_t lastBaseLedColor = LED_OFF;
bool gpsPulseActive = false;
unsigned long lastGpsPulseMs = 0;
unsigned long gpsPulseUntilMs = 0;
 
// System state tracking
bool dataTransferActive = false;
bool canInitializing = false;
bool wifiConnected = false;
bool otaReady = false;
unsigned long lastDataReceivedTime = 0;  // Track when last CAN message was received
const unsigned long DATA_TIMEOUT_MS = 2000;  // 2 seconds timeout for data transfer
 
 // Function prototypes
 bool initializeSDCard();
 bool checkSDCardPresent();
 bool testSDCardAccess();
 bool autoDetectAndInitSDCard();
bool initializeI2CBus();
bool probeI2CAddress(uint8_t addr);
void scanI2CBusOnce();
 bool initializeRTC();
 time_t getRTCTime();
 void setRTCTime(int year, int month, int day, int hour, int minute, int second);
 bool syncTimeFromNTP();
 bool createNewLogFile();
 bool writeFileHeader();
 bool writeCANMessage(const CanFrame& frame, time_t timestamp, uint32_t micros);
 String generateLogFileName();
 void closeCurrentFile();
void setLED(uint32_t color);
void setLEDOverride(uint32_t color);
void updateSystemLED();  // Update LED based on system status
bool initializeWiFi();
bool initializeOTA();
void handleRoot();
void handleFileList();
void handleFileDownload();
void handleFileDelete();
void handleFolderBrowse();
void handleStatus();
bool initCANWithSpeed(int kbps);
bool reinitCANWithSpeed(int kbps);
uint32_t generateFileNonce();
void resetCipherState();
uint8_t encryptByte(uint8_t inputByte);
bool writeEncryptedBuffer(const uint8_t* data, size_t length);
bool writeEncryptedLine(const String& line);
void storeLiveFrame(const CanFrame& frame, time_t timestamp, uint32_t micros, const String& formattedTime);

// IMU (ADXL345) helpers
bool initializeIMU();
bool configureIMURegisters();
bool readIMURaw(int16_t &x, int16_t &y, int16_t &z);
bool calibrateIMU();
void updateIMU();

// GPS (NEO-M8N) helpers
bool initializeGPS();
void updateGPS();
void updateGPSLedPulse();

// ==================== ENCRYPTION FUNCTIONS ====================
uint32_t generateFileNonce() {
    uint32_t randomVal = esp_random();
    randomVal ^= (uint32_t)millis();
    randomVal ^= ((uint32_t)bootMillis << 16);
    if (randomVal == 0) {
        randomVal = millis() ^ 0xA5A5A5A5;
    }
    return randomVal;
}

void resetCipherState() {
    currentCipherState = currentFileNonce ^ 0xA5A5A5A5;
    for (int i = 0; i < 16; i++) {
        currentCipherState = (currentCipherState * 1664525UL) + 1013904223UL + ENCRYPTION_KEY[i];
    }
}

uint8_t encryptByte(uint8_t inputByte) {
    currentCipherState = (currentCipherState * 1664525UL) + 1013904223UL;
    uint8_t keyByte = ENCRYPTION_KEY[currentCipherState & 0x0F];
    uint8_t streamByte = ((currentCipherState >> 24) & 0xFF) ^ keyByte;
    return inputByte ^ streamByte;
}

bool writeEncryptedBuffer(const uint8_t* data, size_t length) {
    if (!logFile) return false;
    for (size_t i = 0; i < length; i++) {
        uint8_t encrypted = encryptByte(data[i]);
        if (logFile.write(encrypted) != 1) {
            Serial.println("ERROR: Failed to write encrypted byte");
            return false;
        }
    }
    currentFileSize += length;
    return true;
}

bool writeEncryptedLine(const String& line) {
    if (!logFile) return false;

    size_t len = line.length();
    for (size_t i = 0; i < len; i++) {
        uint8_t encrypted = encryptByte((uint8_t)line[i]);
        if (logFile.write(encrypted) != 1) {
            Serial.println("ERROR: Failed to write encrypted line");
            closeCurrentFile();
            sdCardReady = false;
            return false;
        }
    }

    uint8_t newline = '\n';
    if (logFile.write(encryptByte(newline)) != 1) {
        Serial.println("ERROR: Failed to terminate encrypted line");
        closeCurrentFile();
        sdCardReady = false;
        return false;
    }

    currentFileSize += (len + 1);
    return true;
}
 
// ==================== LED CONTROL ====================
void setLED(uint32_t color) {
    lastBaseLedColor = color;
    pixels.setPixelColor(0, color);
    pixels.show();
}

void setLEDOverride(uint32_t color) {
    pixels.setPixelColor(0, color);
    pixels.show();
}

// Update LED based on system status
// GREEN: Only when RTC, SD, WiFi STA+AP, CAN, and log file are ready (no active data transfer)
// RED: If any required component is not ready
// MAGENTA: When actively logging data (messages received within last 2 seconds)
// ORANGE: When data logging stops (all systems ready but no messages for 2+ seconds)
void updateSystemLED() {
    bool logReady = sdCardReady && logFile;
    bool allReady = rtcAvailable && canInitialized && logReady;  // WiFi not required for GREEN
    
    // If actively logging data (within timeout), show MAGENTA
    if (dataTransferActive && (millis() - lastDataReceivedTime) < DATA_TIMEOUT_MS) {
        setLED(LED_MAGENTA);
        return;
    }
    
    // If data transfer was active but stopped (timeout), show ORANGE if all systems ready
    if (dataTransferActive && (millis() - lastDataReceivedTime) >= DATA_TIMEOUT_MS) {
        dataTransferActive = false;
        if (allReady) {
            setLED(LED_ORANGE);
            return;
        }
    }
    
    // If we had messages before but stopped, and all systems ready, show ORANGE
    if (messageCount > 0 && allReady && !dataTransferActive) {
        if ((millis() - lastDataReceivedTime) < 5000) {
            setLED(LED_ORANGE);
            return;
        }
    }
    
    // System status LED
    if (allReady) {
        setLED(LED_GREEN);
    } else {
        setLED(LED_RED);
    }
}
 
// ==================== I2C BUS (RTC + IMU shared) ====================
bool initializeI2CBus() {
    WireRTC.end();
    delay(20);
    pinMode(RTC_SDA_PIN, INPUT_PULLUP);
    pinMode(RTC_SCL_PIN, INPUT_PULLUP);
    delay(20);
    WireRTC.begin(RTC_SDA_PIN, RTC_SCL_PIN);
    WireRTC.setClock(100000);
    delay(50);
    i2cReady = true;
    return true;
}

bool probeI2CAddress(uint8_t addr) {
    WireRTC.beginTransmission(addr);
    return (WireRTC.endTransmission() == 0);
}

void scanI2CBusOnce() {
    Serial.println("I2C scan (shared bus):");
    bool foundAny = false;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (probeI2CAddress(addr)) {
            Serial.printf("  - Found device at 0x%02X\n", addr);
            foundAny = true;
        }
        delay(2);
        yield();
    }
    if (!foundAny) {
        Serial.println("  (no I2C devices detected)");
    }
}

// ==================== RTC FUNCTIONS ====================
bool initializeRTC() {
    Serial.println("========================================");
    Serial.println("Initializing RTC on shared I2C bus 0 (pins 22/23)...");
    Serial.print("RTC SDA Pin: ");
    Serial.println(RTC_SDA_PIN);
    Serial.print("RTC SCL Pin: ");
    Serial.println(RTC_SCL_PIN);
    Serial.flush();

    initializeI2CBus();
    
    // Simple retry mechanism (3 attempts)
    bool rtcInitialized = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            Serial.print("RTC initialization attempt ");
            Serial.print(attempt + 1);
            Serial.println(" of 3...");
            delay(200);
            initializeI2CBus();  // re-init bus if the first attempt failed
        }

        WireRTC.setClock(100000);  // 100kHz for DS3231
        delay(50);
        yield();

        // Quick check: Verify DS3231 is present at address 0x68
        if (probeI2CAddress(0x68)) {
            // Device found, try to initialize RTC library
            bool rtcOk = rtc.begin(&WireRTC);
            if (!rtcOk) {
                rtcOk = rtc.begin();
            }
            if (rtcOk) {
                delay(100);  // Give RTC time to stabilize
                
                // Verify RTC is responding by reading time
                DateTime testTime = rtc.now();
                
                if (testTime.year() >= 2000 && testTime.year() <= 2100) {
                    rtcInitialized = true;
                    Serial.println("✓ RTC initialized successfully!");
                    break;
                } else {
                    Serial.print("RTC begin() succeeded but invalid year: ");
                    Serial.println(testTime.year());
                }
            }
        } else if (attempt == 0) {
            Serial.println("DS3231 not found at 0x68 on the shared I2C bus.");
            scanI2CBusOnce();
        }
        
        yield();
    }
    
    if (!rtcInitialized) {
        Serial.println("========================================");
        Serial.println("ERROR: RTC not found after 3 attempts!");
        Serial.println("Check RTC wiring:");
        Serial.printf("  - SDA must be on pin %d (GPIO %d)\n", RTC_SDA_PIN, RTC_SDA_PIN);
        Serial.printf("  - SCL must be on pin %d (GPIO %d)\n", RTC_SCL_PIN, RTC_SCL_PIN);
        Serial.println("  - VCC should be connected to 3.3V or 5V");
        Serial.println("  - GND should be connected to GND");
        Serial.println("  - Verify pull-up resistors (4.7kΩ) are present");
        Serial.println("Falling back to compile-time");
        Serial.println("========================================");
        rtcAvailable = false;
        return false;
    }
    
    yield();
    
    // Check if RTC lost power
    if (rtc.lostPower()) {
        Serial.println("WARNING: RTC lost power! Time may be incorrect.");
        Serial.println("Setting RTC to compile time...");
        
        // Parse compile-time date and time
        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        
        char monthStr[4];
        int year, month, day, hour, minute, second;
        
        sscanf(__DATE__, "%s %d %d", monthStr, &day, &year);
        sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
        
        month = 1;
        for (int i = 0; i < 12; i++) {
            if (strcmp(monthStr, months[i]) == 0) {
                month = i + 1;
                break;
            }
        }
        
        // Set RTC to compile time
        rtc.adjust(DateTime(year, month, day, hour, minute, second));
        Serial.printf("RTC set to compile time: %04d-%02d-%02d %02d:%02d:%02d\n",
                     year, month, day, hour, minute, second);
    } else {
        Serial.println("RTC found and time is valid!");
    }
    
    yield();
    
    // Display current RTC time
    DateTime now = rtc.now();
    Serial.printf("RTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
    
    rtcAvailable = true;
    Serial.println("========================================");
    Serial.println("RTC Status: CONNECTED AND WORKING");
    Serial.println("========================================");
    Serial.flush();
    return true;
}
 
 time_t getRTCTime() {
     if (!rtcAvailable) return 0;
     DateTime now = rtc.now();
     struct tm timeStruct = {0};
     timeStruct.tm_year = now.year() - 1900;
     timeStruct.tm_mon = now.month() - 1;
     timeStruct.tm_mday = now.day();
     timeStruct.tm_hour = now.hour();
     timeStruct.tm_min = now.minute();
     timeStruct.tm_sec = now.second();
     return mktime(&timeStruct);
 }
 
 void setRTCTime(int year, int month, int day, int hour, int minute, int second) {
     if (rtcAvailable) {
         rtc.adjust(DateTime(year, month, day, hour, minute, second));
     }
 }
 
 // ==================== NTP SYNC FUNCTIONS ====================
 bool syncTimeFromNTP() {
     if (WiFi.status() != WL_CONNECTED) {
         return false;
     }
     
     configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov", "time.google.com");
     struct tm timeinfo;
    const uint8_t maxAttempts = 10; // cap ~5 seconds
    for (uint8_t i = 0; i < maxAttempts; i++) {
        if (getLocalTime(&timeinfo)) {
            time_t ntpTime = mktime(&timeinfo);
            if (ntpTime > 0) {
                baseTime = ntpTime;
                bootMillis = millis();
                 if (rtcAvailable) {
                     setRTCTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                 }
                 return true;
             }
         }
         delay(500);
         yield();
    }
    return false;
}

#if ENABLE_WIFI
void handleStatus() {
    String json = "{";
    json += "\"messageCount\":" + String(messageCount) + ",";
    json += "\"dataTransferActive\":" + String(dataTransferActive ? "true" : "false") + ",";
    json += "\"sdCardReady\":" + String(sdCardReady ? "true" : "false") + ",";
    json += "\"canInitialized\":" + String(canInitialized ? "true" : "false") + ",";
    json += "\"rtcAvailable\":" + String(rtcAvailable ? "true" : "false") + ",";
    json += "\"wifiConnected\":" + String(wifiConnected ? "true" : "false") + ",";
    #if ENABLE_OTA
    json += "\"otaReady\":" + String(otaReady ? "true" : "false") + ",";
    json += "\"otaHostname\":\"" + String(OTA_HOSTNAME) + "\",";
    json += "\"otaPort\":" + String(OTA_PORT) + ",";
    #else
    json += "\"otaReady\":false,";
    json += "\"otaHostname\":\"\",";
    json += "\"otaPort\":0,";
    #endif
    json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"staIp\":\"" + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\",";
    json += "\"imuAvailable\":" + String(imuAvailable ? "true" : "false") + ",";
    json += "\"imuHasData\":" + String(imuHasData ? "true" : "false") + ",";
    json += "\"gpsInitialized\":" + String(gpsInitialized ? "true" : "false") + ",";
    json += "\"gpsHasFix\":" + String(gpsHasFix ? "true" : "false") + ",";
    json += "\"currentFileSize\":" + String(currentFileSize) + ",";
    json += "\"currentFile\":\"" + currentLogFileName + "\"";
    json += "}";
    server.send(200, "application/json", json);
}
#endif

// ==================== SD CARD FUNCTIONS ====================
bool checkSDCardPresent() {
     uint8_t cardType = SD.cardType();
     return (cardType != CARD_NONE);
 }
 
 bool testSDCardAccess() {
     File root = SD.open("/", FILE_READ);
     if (!root) {
         return false;
     }
     root.close();
     
     File testFile = SD.open("/_test.tmp", FILE_WRITE);
     if (!testFile) {
         return false;
     }
     
     if (testFile.write('T') != 1) {
         testFile.close();
         SD.remove("/_test.tmp");
         return false;
     }
     
     testFile.flush();
     testFile.close();
     SD.remove("/_test.tmp");
     
     return true;
 }
 
 bool initializeSDCard() {
     Serial.println("Initializing SD Card...");
     
     // Note: SPI should already be initialized in setup(), but ensure it's ready
     SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
     delay(100);  // Give SPI time to stabilize
     
     // Initialize SD card
     if (!SD.begin(SD_CS_PIN, SPI, SD_CARD_SPEED)) {
         Serial.println("ERROR: SD Card initialization failed!");
         Serial.println("Check SD card connection and format (FAT32)");
         Serial.println("SD Card Status: NOT DETECTED");
         return false;
     }
     
     // Check card type and size
     uint8_t cardType = SD.cardType();
     if (cardType == CARD_NONE) {
         Serial.println("ERROR: No SD card found!");
         Serial.println("SD Card Status: NOT DETECTED");
         return false;
     }
     
     Serial.print("SD Card Type: ");
     if (cardType == CARD_MMC) {
         Serial.println("MMC");
     } else if (cardType == CARD_SD) {
         Serial.println("SDSC");
     } else if (cardType == CARD_SDHC) {
         Serial.println("SDHC");
     } else {
         Serial.println("UNKNOWN");
     }
     
     // Get card size
     uint64_t cardSize = SD.cardSize() / (1024 * 1024);
     Serial.printf("SD Card Size: %lluMB\n", cardSize);
     
     if (cardSize < 100) {
         Serial.println("WARNING: SD card size is small (< 100MB)");
     }
     
     // CRITICAL: Test actual card access to verify authentication
     Serial.println("Testing SD card access and authentication...");
     if (!testSDCardAccess()) {
         Serial.println("ERROR: SD Card authentication failed!");
         Serial.println("SD card may be corrupted, write-protected, or incompatible");
         Serial.println("SD Card Status: AUTHENTICATION FAILED");
         return false;
     }
     
     Serial.println("SD Card access test: PASSED");
     Serial.println("SD Card initialized successfully!");
     Serial.println("SD Card Status: READY AND WORKING");
     return true;
 }
 
 bool autoDetectAndInitSDCard() {
     if (sdCardReady) {
         return true;
     }
     
     if (SD.begin(SD_CS_PIN, SPI, SD_CARD_SPEED)) {
         uint8_t cardType = SD.cardType();
         if (cardType != CARD_NONE) {
             // Test card access before proceeding
             if (!testSDCardAccess()) {
                 Serial.println("SD Card detected but authentication failed!");
                 return false;
             }
             
             if (rtcAvailable) {
                 baseTime = getRTCTime();
                 bootMillis = millis();
             } else if (baseTime == 0) {
                 const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                 char monthStr[4];
                 int year, month, day, hour, minute, second;
                 sscanf(__DATE__, "%s %d %d", monthStr, &day, &year);
                 sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
                 month = 1;
                 for (int i = 0; i < 12; i++) {
                     if (strcmp(monthStr, months[i]) == 0) {
                         month = i + 1;
                         break;
                     }
                 }
                 struct tm timeStruct = {0};
                 timeStruct.tm_year = year - 1900;
                 timeStruct.tm_mon = month - 1;
                 timeStruct.tm_mday = day;
                 timeStruct.tm_hour = hour;
                 timeStruct.tm_min = minute;
                 timeStruct.tm_sec = second;
                 baseTime = mktime(&timeStruct);
                 bootMillis = millis();
             }
             
            // Defer log file creation until we are actually receiving data (CAN + IMU).
            sdCardReady = true;
            Serial.println("SD Card auto-detected and initialized! (Logging will start when data flows)");
            return true;
         }
     }
     
     return false;
 }
 
String generateLogFileName() {
    // Generate .NXT filename with timestamp in CAN_Logged_Data folder
    // Format: /CAN_Logged_Data/CAN_LOG_YYYYMMDD_HHMMSS.NXT
    struct tm* timeinfo = localtime(&baseTime);
    char fileName[64];
    sprintf(fileName, "/CAN_Logged_Data/%s%04d%02d%02d_%02d%02d%02d.NXT",
            LOG_FILE_PREFIX,
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec);
    return String(fileName);
}

bool createNewLogFile() {
    if (logFile) {
        Serial.println("Closing existing log file...");
        logFile.flush();
        logFile.close();
        delay(100);  // Allow file system to complete close
    }

    // Ensure CAN_Logged_Data folder exists before creating file
    if (!SD.exists("/CAN_Logged_Data")) {
        Serial.println("Creating CAN_Logged_Data folder before creating log file...");
        if (!SD.mkdir("/CAN_Logged_Data")) {
            Serial.println("ERROR: Failed to create CAN_Logged_Data folder!");
            Serial.print("  SD card type: ");
            Serial.println(SD.cardType());
            Serial.print("  SD card size: ");
            Serial.print(SD.cardSize() / (1024 * 1024));
            Serial.println(" MB");
            return false;
        }
        Serial.println("CAN_Logged_Data folder created successfully");
    }

    currentLogFileName = generateLogFileName();
    Serial.print("Creating new log file: ");
    Serial.println(currentLogFileName);

    logFile = SD.open(currentLogFileName, FILE_WRITE);
    if (!logFile) {
        Serial.println("ERROR: Failed to open log file for writing!");
        Serial.print("  File path: ");
        Serial.println(currentLogFileName);
        return false;
    }

    if (!writeFileHeader()) {
        Serial.println("ERROR: Failed to write file header!");
        logFile.close();
        return false;
    }

    // Force flush after header write to ensure it's written to disk
    logFile.flush();
    
    // Verify file was created and is writable
    if (!logFile) {
        Serial.println("ERROR: Log file is NULL after creation!");
        return false;
    }
    
    currentFileSize = logFile.size();
    frameSequence = 0;
    
    // Verify file size is at least header size (encrypted CSV header follows)
    uint32_t actualSize = logFile.size();
    if (actualSize < NXT_HEADER_SIZE) {
        Serial.printf("WARNING: New file size is %lu bytes, expected at least %d bytes (header)\n", actualSize, NXT_HEADER_SIZE);
    }
    
    Serial.print("New log file created successfully! Initial size: ");
    Serial.print(currentFileSize);
    Serial.print(" bytes (actual: ");
    Serial.print(actualSize);
    Serial.println(" bytes)");
    Serial.print("File path: ");
    Serial.println(currentLogFileName);
    Serial.println("File is ready to receive CAN data");
    return true;
}

// ==================== IMU FUNCTIONS (ADXL345) ====================
// Minimal register-level driver (no external library required).
// Output is linear acceleration in m/s^2.

static bool adxlWrite(uint8_t reg, uint8_t value) {
    WireRTC.beginTransmission(ADXL345_ADDR);
    WireRTC.write(reg);
    WireRTC.write(value);
    return (WireRTC.endTransmission() == 0);
}

static bool adxlRead(uint8_t reg, uint8_t* buf, size_t len) {
    WireRTC.beginTransmission(ADXL345_ADDR);
    WireRTC.write(reg);
    if (WireRTC.endTransmission(false) != 0) {
        return false;
    }
    size_t read = WireRTC.requestFrom(ADXL345_ADDR, (uint8_t)len);
    if (read != len) {
        // Drain if partial
        while (WireRTC.available()) (void)WireRTC.read();
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = WireRTC.read();
    }
    return true;
}

bool initializeIMU() {
    // Ensure bus is started (RTC init should have done this, but safe to call)
    if (!i2cReady) {
        initializeI2CBus();
    }
    WireRTC.setClock(100000);

    // Quick probe: read DEVID (0x00) expected 0xE5
    uint8_t devid = 0;
    bool found = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (adxlRead(0x00, &devid, 1) && devid == 0xE5) {
            found = true;
            break;
        }
        Serial.println("IMU probe failed, reinitializing I2C bus...");
        initializeI2CBus();
        delay(50);
    }
    if (!found) {
        return false;
    }

    if (!configureIMURegisters()) {
        return false;
    }

    // First read to seed values
    int16_t x, y, z;
    if (readIMURaw(x, y, z)) {
        imuHasData = true;
    }

    // Mark IMU available before calibration
    imuAvailable = true;

    // Calibrate IMU offsets with 100 samples
    if (calibrateIMU()) {
        imuCalibrated = true;
        Serial.println("IMU calibration: DONE (100 samples)");
    } else {
        imuCalibrated = false;
        Serial.println("IMU calibration: FAILED");
    }

    return true;
}

bool readIMURaw(int16_t &x, int16_t &y, int16_t &z) {
    uint8_t buf[6];
    // DATAX0 register = 0x32
    if (!adxlRead(0x32, buf, 6)) {
        return false;
    }
    x = (int16_t)((buf[1] << 8) | buf[0]);
    y = (int16_t)((buf[3] << 8) | buf[2]);
    z = (int16_t)((buf[5] << 8) | buf[4]);
    return true;
}

bool calibrateIMU() {
    if (!imuAvailable) {
        return false;
    }
    long sumX = 0;
    long sumY = 0;
    long sumZ = 0;
    float sumMag = 0.0f;
    int count = 0;
    int attempts = 0;

    Serial.println("IMU calibration: collecting 100 samples...");
    while (count < IMU_CALIBRATION_SAMPLES && attempts < (IMU_CALIBRATION_SAMPLES * 2)) {
        int16_t x, y, z;
        if (readIMURaw(x, y, z)) {
            sumX += x;
            sumY += y;
            sumZ += z;
            float fx = (float)x;
            float fy = (float)y;
            float fz = (float)z;
            sumMag += sqrtf((fx * fx) + (fy * fy) + (fz * fz));
            count++;
            Serial.printf("IMU sample %03d/%03d -> raw X:%d Y:%d Z:%d\n",
                          count, IMU_CALIBRATION_SAMPLES, (int)x, (int)y, (int)z);
        }
        attempts++;
        delay(5);
        yield();
    }

    if (count < IMU_CALIBRATION_SAMPLES) {
        Serial.printf("IMU calibration: only %d/%d samples collected\n", count, IMU_CALIBRATION_SAMPLES);
        return false;
    }

    imuOffsetX = (float)sumX / (float)count;
    imuOffsetY = (float)sumY / (float)count;
    imuOffsetZ = (float)sumZ / (float)count;
    Serial.printf("IMU calibration offsets -> X: %.2f Y: %.2f Z: %.2f (raw counts)\n",
                  imuOffsetX, imuOffsetY, imuOffsetZ);

    // Compute a scale correction so gravity magnitude is close to 9.80665 m/s^2
    float avgMag = sumMag / (float)count;
    if (avgMag > 1.0f) {
        float magMs2 = avgMag * ADXL_G_PER_LSB * ADXL_G_TO_MS2;
        if (magMs2 > 0.1f) {
            imuScale = ADXL_G_TO_MS2 / magMs2;
        } else {
            imuScale = 1.0f;
        }
    } else {
        imuScale = 1.0f;
    }
    Serial.printf("IMU scale correction -> %.4f (gravity target %.3f m/s^2)\n", imuScale, ADXL_G_TO_MS2);
    return true;
}

bool configureIMURegisters() {
    // DATA_FORMAT (0x31): FULL_RES=1, range=+/-2g (0)
    if (!adxlWrite(0x31, 0x08)) {
        return false;
    }
    // BW_RATE (0x2C): 3200 Hz (0x0F) for 1 kHz+ sampling
    (void)adxlWrite(0x2C, 0x0F);
    // POWER_CTL (0x2D): Measure=1
    if (!adxlWrite(0x2D, 0x08)) {
        return false;
    }
    return true;
}

void updateIMU() {
    if (!imuAvailable) {
        return;
    }
    const unsigned long nowUs = micros();
    // 1 kHz sampling (or faster if configured)
    if ((unsigned long)(nowUs - lastImuReadUs) < IMU_SAMPLE_INTERVAL_US) {
        return;
    }
    lastImuReadUs = nowUs;

    int16_t xRaw, yRaw, zRaw;
    static uint8_t imuFailCount = 0;
    if (!readIMURaw(xRaw, yRaw, zRaw)) {
        imuFailCount++;
        if (imuFailCount >= 5) {
            imuFailCount = 0;
            // Attempt to recover I2C + IMU registers without blocking logging
            initializeI2CBus();
            WireRTC.setClock(100000);
            (void)configureIMURegisters();
        }
        return;
    }
    imuFailCount = 0;

    float x = (float)xRaw;
    float y = (float)yRaw;
    float z = (float)zRaw;
    if (imuCalibrated) {
        x -= imuOffsetX;
        y -= imuOffsetY;
        z -= imuOffsetZ;
    }

    // Convert to m/s^2 and apply scale correction
    const float ax = x * ADXL_G_PER_LSB * ADXL_G_TO_MS2 * imuScale;
    const float ay = y * ADXL_G_PER_LSB * ADXL_G_TO_MS2 * imuScale;
    const float az = z * ADXL_G_PER_LSB * ADXL_G_TO_MS2 * imuScale;

    // Gravity magnitude from raw (no offset) to track ~9.8 m/s^2 at rest
    const float gMag = sqrtf((float)xRaw * (float)xRaw +
                             (float)yRaw * (float)yRaw +
                             (float)zRaw * (float)zRaw) *
                       ADXL_G_PER_LSB * ADXL_G_TO_MS2 * imuScale;

    // Simple low-pass filter for smoothness
    const float alpha = 0.2f;
    linearAccelX = (1.0f - alpha) * linearAccelX + alpha * ax;
    linearAccelY = (1.0f - alpha) * linearAccelY + alpha * ay;
    linearAccelZ = (1.0f - alpha) * linearAccelZ + alpha * az;
    gravityAccel = (1.0f - alpha) * gravityAccel + alpha * gMag;
    imuHasData = true;
}

// ==================== GPS FUNCTIONS (NEO-M8N) ====================
static double nmeaToDecimal(const char* val, char hemi) {
    if (!val || !*val) return 0.0;
    double v = atof(val);
    int deg = (int)(v / 100.0);
    double minutes = v - (deg * 100.0);
    double dec = (double)deg + (minutes / 60.0);
    if (hemi == 'S' || hemi == 'W') {
        dec = -dec;
    }
    return dec;
}

static void formatGpsTime(const char* hhmmss, char* out, size_t outLen) {
    if (!hhmmss || strlen(hhmmss) < 6 || outLen < 9) {
        strncpy(out, "0", outLen);
        return;
    }
    char buf[9] = {0};
    buf[0] = hhmmss[0];
    buf[1] = hhmmss[1];
    buf[2] = ':';
    buf[3] = hhmmss[2];
    buf[4] = hhmmss[3];
    buf[5] = ':';
    buf[6] = hhmmss[4];
    buf[7] = hhmmss[5];
    strncpy(out, buf, outLen);
}

static void stripChecksum(char* token) {
    if (!token) return;
    char* star = strchr(token, '*');
    if (star) {
        *star = '\0';
    }
}

static void parseGPRMC(char* line) {
    // $GPRMC,hhmmss.sss,A,llll.ll,a,yyyyy.yy,a,speed,course,date,...
    char* fields[16] = {0};
    int idx = 0;
    char* tok = strtok(line, ",");
    while (tok && idx < 16) {
        fields[idx++] = tok;
        tok = strtok(NULL, ",");
    }
    if (idx < 10) return;

    const char* timeStr = fields[1];
    const char* status = fields[2];
    const char* latStr = fields[3];
    const char* latHem = fields[4];
    const char* lonStr = fields[5];
    const char* lonHem = fields[6];
    const char* spdStr = fields[7];
    const char* crsStr = fields[8];

    if (!status || status[0] != 'A') {
        return;
    }

    stripChecksum((char*)spdStr);
    stripChecksum((char*)crsStr);

    double lat = nmeaToDecimal(latStr, latHem ? latHem[0] : 'N');
    double lon = nmeaToDecimal(lonStr, lonHem ? lonHem[0] : 'E');
    float speedKmh = spdStr ? (float)(atof(spdStr) * 1.852) : 0.0f;
    float course = crsStr ? (float)atof(crsStr) : 0.0f;

    if (lat != 0.0 || lon != 0.0) {
        gpsLat = (float)lat;
        gpsLon = (float)lon;
    }
    gpsSpeed = speedKmh;
    gpsCourse = course;
    formatGpsTime(timeStr, gpsTimeText, sizeof(gpsTimeText));
    gpsHasFix = true;
    gpsLastFixMs = millis();
}

static void parseGPGGA(char* line) {
    // $GPGGA,hhmmss.sss,lat,NS,lon,EW,quality,numSV,HDOP,alt,M,...
    char* fields[16] = {0};
    int idx = 0;
    char* tok = strtok(line, ",");
    while (tok && idx < 16) {
        fields[idx++] = tok;
        tok = strtok(NULL, ",");
    }
    if (idx < 10) return;

    const char* timeStr = fields[1];
    const char* latStr = fields[2];
    const char* latHem = fields[3];
    const char* lonStr = fields[4];
    const char* lonHem = fields[5];
    const char* qualStr = fields[6];
    const char* satsStr = fields[7];
    const char* hdopStr = fields[8];
    const char* altStr = fields[9];

    stripChecksum((char*)altStr);

    int quality = qualStr ? atoi(qualStr) : 0;
    if (quality <= 0) {
        return;
    }

    double lat = nmeaToDecimal(latStr, latHem ? latHem[0] : 'N');
    double lon = nmeaToDecimal(lonStr, lonHem ? lonHem[0] : 'E');

    if (lat != 0.0 || lon != 0.0) {
        gpsLat = (float)lat;
        gpsLon = (float)lon;
    }
    gpsAlt = altStr ? (float)atof(altStr) : 0.0f;
    gpsSats = satsStr ? atoi(satsStr) : 0;
    gpsHdop = hdopStr ? (float)atof(hdopStr) : 0.0f;
    formatGpsTime(timeStr, gpsTimeText, sizeof(gpsTimeText));
    gpsHasFix = true;
    gpsLastFixMs = millis();
}

bool initializeGPS() {
    Serial.println("Step 6: Initializing GPS (NEO-M8N)...");
    GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    gpsInitialized = true;
    gpsHasFix = false;
    gpsLat = 0.0f;
    gpsLon = 0.0f;
    gpsAlt = 0.0f;
    gpsSpeed = 0.0f;
    gpsCourse = 0.0f;
    gpsSats = 0;
    gpsHdop = 0.0f;
    strncpy(gpsTimeText, "0", sizeof(gpsTimeText));
    gpsLastFixMs = 0;
    Serial.printf("GPS UART started on RX=%d TX=%d @ %d baud\n", GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
    return true;
}

void updateGPS() {
    if (!gpsInitialized) {
        return;
    }

    static char nmeaLine[128];
    static size_t nmeaPos = 0;

    while (GPSSerial.available()) {
        char c = (char)GPSSerial.read();
        if (c == '\n') {
            nmeaLine[nmeaPos] = '\0';
            nmeaPos = 0;
            if (nmeaLine[0] == '$') {
                if (strncmp(nmeaLine, "$GPRMC", 6) == 0 || strncmp(nmeaLine, "$GNRMC", 6) == 0) {
                    parseGPRMC(nmeaLine);
                } else if (strncmp(nmeaLine, "$GPGGA", 6) == 0 || strncmp(nmeaLine, "$GNGGA", 6) == 0) {
                    parseGPGGA(nmeaLine);
                }
            }
        } else if (c != '\r') {
            if (nmeaPos < (sizeof(nmeaLine) - 1)) {
                nmeaLine[nmeaPos++] = c;
            } else {
                nmeaPos = 0;
            }
        }
    }

    if (gpsHasFix && (millis() - gpsLastFixMs) > GPS_FIX_TIMEOUT_MS) {
        gpsHasFix = false;
    }
}

void updateGPSLedPulse() {
    if (!gpsInitialized || dataTransferActive) {
        return;
    }

    unsigned long now = millis();
    if (gpsPulseActive) {
        if (now >= gpsPulseUntilMs) {
            gpsPulseActive = false;
            setLED(lastBaseLedColor);
        }
        return;
    }

    if (now - lastGpsPulseMs >= 3000) {
        gpsPulseActive = true;
        gpsPulseUntilMs = now + 120;
        lastGpsPulseMs = now;
        if (gpsHasFix) {
            setLEDOverride(LED_GREEN);
        } else {
            setLEDOverride(LED_YELLOW);
        }
    }
}

bool writeFileHeader() {
    if (!logFile) return false;

    currentFileNonce = generateFileNonce();

    uint8_t header[NXT_HEADER_SIZE] = {0};
    memcpy(header, NXT_LOG_SIGNATURE, 6);
    header[6] = NXT_LOG_VERSION;
    header[7] = NXT_HEADER_SIZE;
    memcpy(&header[8], &currentFileNonce, sizeof(currentFileNonce));
    uint32_t reserved = 0;
    memcpy(&header[12], &reserved, sizeof(reserved));

    size_t written = logFile.write(header, NXT_HEADER_SIZE);
    if (written != NXT_HEADER_SIZE) {
        Serial.println("ERROR: Failed to write encrypted log header");
        return false;
    }

    currentFileSize = logFile.size();
    resetCipherState();

    String headerLine = "Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC";
    for (int i = 0; i < 8; i++) {
        headerLine += ",Data" + String(i);
    }
    // Linear acceleration from IMU (ADXL345) used by decoder/new.csv
    headerLine += ",LinearAccelX,LinearAccelY,LinearAccelZ,Gravity";
    // GPS columns (optional)
    headerLine += ",GPS_Lat,GPS_Lon,GPS_Alt,GPS_Speed,GPS_Course,GPS_Sats,GPS_HDOP,GPS_Time";
    bool result = writeEncryptedLine(headerLine);
    if (result) {
        logFile.flush();
    }
    return result;
}

bool writeCANMessage(const CanFrame& frame, time_t timestamp, uint32_t micros) {
    if (!sdCardReady) {
        // Attempt to recover SD + log file if a CAN message arrives
        if (!initializeSDCard() || !createNewLogFile()) {
            Serial.println("ERROR: SD not ready and recovery failed!");
            sdCardReady = false;
            return false;
        }
        sdCardReady = true;
    }
    if (!logFile) {
        if (!createNewLogFile()) {
            Serial.println("ERROR: Log file not open and recovery failed!");
            sdCardReady = false;
            return false;
        }
    }

    // Format timestamp string
    struct tm* timeinfo = localtime(&timestamp);
    char timeStr[32];
    sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    String line;
    line.reserve(128);
    line += timeStr;
    line += ",";
    line += String((unsigned long)timestamp);
    line += ",";
    line += String((unsigned long)micros);
    line += ",";

    String idStr = String(frame.identifier, HEX);
    idStr.toUpperCase();
    line += idStr;

    line += ",";
    line += frame.extd ? "1" : "0";
    line += ",";
    line += frame.rtr ? "1" : "0";
    line += ",";
    line += String(frame.data_length_code);

    uint8_t dlc = frame.data_length_code;
    if (dlc > 8) dlc = 8;
    for (int i = 0; i < 8; i++) {
        line += ",";
        if (i < dlc) {
            if (frame.data[i] < 0x10) {
                line += "0";
            }
            String dataStr = String(frame.data[i], HEX);
            dataStr.toUpperCase();
            line += dataStr;
        } else {
            line += "00";
        }
    }

    // Add IMU linear acceleration values (m/s^2) and gravity magnitude.
    // If IMU is not available yet, write zeros so logging never blocks.
    line += ",";
    line += String((imuAvailable && imuHasData) ? linearAccelX : 0.0f, 4);
    line += ",";
    line += String((imuAvailable && imuHasData) ? linearAccelY : 0.0f, 4);
    line += ",";
    line += String((imuAvailable && imuHasData) ? linearAccelZ : 0.0f, 4);
    line += ",";
    line += String((imuAvailable && imuHasData) ? gravityAccel : 0.0f, 4);
    
    // GPS columns (use zeros if no fix)
    if (gpsHasFix) {
        line += ",";
        line += String(gpsLat, 6);
        line += ",";
        line += String(gpsLon, 6);
        line += ",";
        line += String(gpsAlt, 2);
        line += ",";
        line += String(gpsSpeed, 2);
        line += ",";
        line += String(gpsCourse, 2);
        line += ",";
        line += String(gpsSats);
        line += ",";
        line += String(gpsHdop, 2);
        line += ",";
        line += String(gpsTimeText);
    } else {
        line += ",0,0,0,0,0,0,0,0";
    }
    if (!writeEncryptedLine(line)) {
        Serial.println("ERROR: Failed to write encrypted CSV line");
        return false;
    }

    // Optional integrity check: track actual file size periodically
    static uint32_t writeCount = 0;
    writeCount++;
    if (writeCount % 50 == 0 && logFile) {
        uint32_t actualSize = logFile.size();
        if (actualSize < currentFileSize) {
            Serial.printf("WARNING: File size mismatch (expected >= %lu, actual %lu)\n", currentFileSize, actualSize);
        }
    }

    // Flush more frequently to ensure data is written
    flushCounter++;
    static uint32_t earlyFlushCount = 0;
    if (earlyFlushCount < 5) {
        logFile.flush();
        earlyFlushCount++;
    }
    if (flushCounter >= FLUSH_INTERVAL) {
        logFile.flush();
        flushCounter = 0;
        yield();
    }

    return true;
 }
 
 void storeLiveFrame(const CanFrame& frame, time_t timestamp, uint32_t micros, const String& formattedTime) {
     uint32_t nextSeq = liveFrameCounter + 1;
     LiveFrame &slot = liveFrameBuffer[(nextSeq - 1) % LIVE_BUFFER_SIZE];
     
     slot.seq = nextSeq;
     slot.unixTime = timestamp;
     slot.micros = micros;
     formattedTime.toCharArray(slot.timeText, sizeof(slot.timeText));
     slot.identifier = frame.identifier;
     slot.extended = frame.extd;
     slot.rtr = frame.rtr;
     slot.dlc = frame.data_length_code;
     
     for (int i = 0; i < frame.data_length_code && i < 8; i++) {
         slot.data[i] = frame.data[i];
     }
     for (int i = frame.data_length_code; i < 8; i++) {
         slot.data[i] = 0;
     }
     
     liveFrameCounter = nextSeq;
 }
 
void closeCurrentFile() {
    if (logFile) {
        Serial.println("Closing log file...");
        logFile.flush();
        logFile.close();
        Serial.print("Final file size: ");
        Serial.print(currentFileSize);
        Serial.println(" bytes");
        logFile = File();  // Explicitly clear the file object
    }
}
 
 // ==================== CAN FUNCTIONS ====================
 bool initCANWithSpeed(int kbps) {
    canInitializing = true;
    setLED(LED_YELLOW);  // CAN Bus Initializing

    Serial.printf("Initializing CAN bus at %d kbps...\n", kbps);

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = CAN_RX_QUEUE_LEN;
    g_config.tx_queue_len = 10;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
#if defined(TWAI_TIMING_CONFIG_1MBITS)
    if (kbps == 1000) {
        t_config = TWAI_TIMING_CONFIG_1MBITS();
    }
#endif
    if (kbps == 800) {
        t_config = TWAI_TIMING_CONFIG_800KBITS();
    } else if (kbps == 500) {
        t_config = TWAI_TIMING_CONFIG_500KBITS();
    } else if (kbps == 250) {
        t_config = TWAI_TIMING_CONFIG_250KBITS();
    } else if (kbps == 125) {
        t_config = TWAI_TIMING_CONFIG_125KBITS();
    }

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t result = twai_driver_install(&g_config, &t_config, &f_config);
    if (result != ESP_OK) {
        canInitializing = false;
        setLED(LED_RED);
        Serial.print("ERROR: CAN Bus initialization failed! Error Code: ");
        Serial.println(result);
        Serial.println("Check CAN transceiver connections");
        return false;
    }

    result = twai_start();
    if (result != ESP_OK) {
        twai_driver_uninstall();
        canInitializing = false;
        setLED(LED_RED);
        Serial.print("ERROR: CAN Bus start failed! Error Code: ");
        Serial.println(result);
        Serial.println("Check CAN transceiver power and connections");
        return false;
    }

    canInitialized = true;
    canInitializing = false;
    currentCanSpeedKbps = kbps;
    Serial.printf("CAN Bus initialized successfully! Speed: %d kbps\n", kbps);
    return true;
}

bool initCAN() {
    return initCANWithSpeed(CAN_SPEED_KBPS);
}

bool reinitCANWithSpeed(int kbps) {
    if (canInitialized) {
        twai_stop();
        twai_driver_uninstall();
        canInitialized = false;
    }
    return initCANWithSpeed(kbps);
}

// ==================== WIFI FUNCTIONS ====================
 #if ENABLE_WIFI
 bool initializeWiFi() {
     setLED(LED_BLUE);  // Initializing WiFi
     Serial.println("Initializing WiFi (AP + STA)...");
     
     // Configure WiFi as both AP and STA
     WiFi.mode(WIFI_AP_STA);
     
     // Configure Access Point
     if (!WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET)) {
         Serial.println("ERROR: Failed to configure WiFi AP!");
         return false;
     }
     
     // Start Access Point
     if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS)) {
         Serial.println("ERROR: Failed to start WiFi AP!");
         return false;
     }
     
     Serial.print("WiFi AP started: ");
     Serial.println(WIFI_AP_SSID);
     Serial.print("IP address: ");
     Serial.println(WiFi.softAPIP());
     Serial.print("Password: ");
     Serial.println(WIFI_AP_PASS);
     Serial.println("Connect to this network and open http://192.168.10.1 in your browser");
 
     // Try to connect as a Station (optional; uses placeholders)
     if (strlen(WIFI_STA_SSID) > 0 && strcmp(WIFI_STA_SSID, "YOUR_STA_SSID") != 0) {
         Serial.print("Connecting to STA network: ");
         Serial.println(WIFI_STA_SSID);
         WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
         
         unsigned long startAttempt = millis();
         const unsigned long connectTimeout = 8000;  // Reduced timeout so AP is available faster
         while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < connectTimeout) {
             delay(200);
             yield();
         }
         
            if (WiFi.status() == WL_CONNECTED) {
                wifiConnected = true;
                setLED(LED_CYAN);  // STA+AP up (system may still be initializing)
                Serial.println("========================================");
                Serial.println("[WiFi] :: STA Connection: SUCCESS!");
                Serial.println("[WiFi] :: Connection successfully setup!");
                Serial.println("========================================");
                Serial.print("[WiFi] :: STA IP Address: ");
                Serial.println(WiFi.localIP());
                Serial.print("[WiFi] :: AP IP Address: ");
                Serial.println(WiFi.softAPIP());
                Serial.println("[WiFi] :: Running in DUAL MODE (AP + STA)");
                if (syncTimeFromNTP()) {
                    Serial.println("[NTP] :: Time synchronized successfully!");
                    ntpSynced = true;
                    lastNtpSyncMs = millis();
                } else {
                    Serial.println("[NTP] :: Time sync failed (will retry later)");
                }
            } else {
             Serial.println("[WiFi] :: STA connection failed (AP still active and visible)!");
             Serial.println("[WiFi] :: Running in AP-ONLY mode");
             // Keep LED CYAN since AP is working and visible
             if (WiFi.softAPgetStationNum() >= 0) {
                 setLED(LED_CYAN);  // AP is active and visible
             }
         }
     } else {
         // No STA configured, just AP mode
         Serial.println("[WiFi] :: Running in AP-ONLY mode");
         setLED(LED_CYAN);  // AP active and visible
     }
     
     Serial.println("========================================");
     Serial.println("[WiFi] :: WiFi Setup: COMPLETE!");
     Serial.println("[WiFi] :: Access Point is READY for PC connection!");
     Serial.println("========================================");
     return true;
 }
 #endif

// ==================== OTA FUNCTIONS ====================
#if ENABLE_WIFI && ENABLE_OTA
bool initializeOTA() {
    Serial.println("[OTA] Initializing OTA update service...");

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);

    ArduinoOTA
        .onStart([]() {
            Serial.println("[OTA] Update started");
        })
        .onEnd([]() {
            Serial.println("[OTA] Update complete");
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            uint32_t percent = (total > 0) ? (progress * 100U / total) : 0;
            Serial.printf("[OTA] Progress: %u%%\n", percent);
        })
        .onError([](ota_error_t error) {
            Serial.printf("[OTA] Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed");
            else if (error == OTA_END_ERROR) Serial.println("End failed");
            else Serial.println("Unknown error");
        });

    ArduinoOTA.begin();
    otaReady = true;

    Serial.println("[OTA] OTA server initialized");
    Serial.print("[OTA] Hostname: ");
    Serial.println(OTA_HOSTNAME);
    Serial.print("[OTA] Port: ");
    Serial.println(OTA_PORT);
    Serial.print("[OTA] AP IP: ");
    Serial.println(WiFi.softAPIP());
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[OTA] STA IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[OTA] STA not connected (AP-only mode)");
    }
    Serial.println("[OTA] Arduino IDE -> Tools -> Port -> Network Ports");
    return true;
}
#endif
 
// ==================== WEB SERVER FUNCTIONS ====================
#if ENABLE_WIFI
bool isDataFileName(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".nxt") || lower.endsWith(".csv");
}

 void handleRoot() {
     String html = "<!DOCTYPE html><html><head>";
     html += "<meta charset='UTF-8'>";
     html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
     html += "<title>CAN Data Logger - Advanced Control Panel</title>";
     html += "<style>";
     html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
     html += "body { font-family: 'Segoe UI', 'Roboto', 'Inter', sans-serif; background: radial-gradient(1200px 600px at 20% -10%, #e0e7ff 0%, #f8fafc 45%, #f1f5f9 100%); color: #1f2937; min-height: 100vh; padding: 0; }";
     html += ".container { max-width: 1600px; margin: 0 auto; padding: 20px; }";
     html += ".header { background: linear-gradient(135deg, #8b5cf6 0%, #7c3aed 50%, #6d28d9 100%); padding: 40px; border-radius: 20px; margin-bottom: 30px; box-shadow: 0 20px 60px rgba(139, 92, 246, 0.5), 0 0 0 1px rgba(139, 92, 246, 0.3); }";
     html += ".header h1 { font-size: 38px; font-weight: 800; color: #ffffff; margin-bottom: 10px; text-shadow: 0 4px 12px rgba(0,0,0,0.4); letter-spacing: -0.5px; }";
     html += ".header p { color: #e9d5ff; font-size: 17px; opacity: 0.95; font-weight: 500; }";
     html += ".stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 20px; margin-bottom: 30px; }";
     html += ".stat-card { background: #ffffff; padding: 25px; border-radius: 16px; border: 1px solid #e5e7eb; transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1); box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
     html += ".stat-card:hover { transform: translateY(-6px) scale(1.02); box-shadow: 0 12px 32px rgba(139, 92, 246, 0.2), 0 0 0 1px rgba(139, 92, 246, 0.2); border-color: rgba(139, 92, 246, 0.4); }";
     html += ".stat-card.ok { border-left: 5px solid #10b981; background: #ffffff; }";
     html += ".stat-card.error { border-left: 5px solid #ef4444; background: #ffffff; }";
     html += ".stat-card.warning { border-left: 5px solid #f59e0b; background: #ffffff; }";
     html += ".stat-label { font-size: 13px; color: #6b7280; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px; font-weight: 600; }";
     html += ".stat-value { font-size: 30px; font-weight: 800; color: #1f2937; text-shadow: none; }";
     html += ".stat-desc { font-size: 12px; color: #9ca3af; margin-top: 6px; font-weight: 500; }";
     html += ".section { background: #ffffff; padding: 30px; border-radius: 16px; margin: 30px 0; border: 1px solid #e5e7eb; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
     html += ".section-title { font-size: 26px; font-weight: 700; color: #1f2937; margin-bottom: 20px; display: flex; align-items: center; gap: 12px; text-shadow: none; }";
     html += ".file-list { display: grid; gap: 12px; }";
    html += ".file-list-container { max-height: 600px; overflow-y: auto; overflow-x: hidden; padding: 10px; margin: 10px 0; }";
    html += ".file-list-container::-webkit-scrollbar { width: 8px; }";
    html += ".file-list-container::-webkit-scrollbar-track { background: #f1f1f1; border-radius: 10px; }";
    html += ".file-list-container::-webkit-scrollbar-thumb { background: #8b5cf6; border-radius: 10px; }";
    html += ".file-list-container::-webkit-scrollbar-thumb:hover { background: #7c3aed; }";
    html += ".file-item { background: #f9fafb; padding: 15px 20px; border-radius: 12px; border: 1px solid #e5e7eb; display: flex; justify-content: space-between; align-items: center; transition: all 0.3s; margin-bottom: 8px; }";
    html += ".file-item:hover { background: #ffffff; border-color: rgba(139, 92, 246, 0.4); transform: translateX(4px); box-shadow: 0 4px 12px rgba(139, 92, 246, 0.15); }";
    html += ".file-item.selected { background: #ede9fe; border-color: #8b5cf6; border-width: 2px; }";
    html += ".file-item.active { border-color: #10b981; box-shadow: 0 0 0 1px rgba(16, 185, 129, 0.3), 0 8px 18px rgba(16, 185, 129, 0.15); }";
    html += ".badge { display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 11px; font-weight: 700; letter-spacing: 0.5px; margin-right: 8px; }";
    html += ".badge-active { background: #10b981; color: #ffffff; }";
    html += ".badge-latest { background: #f59e0b; color: #111827; }";
    html += ".file-item input[type='checkbox'] { width: 18px; height: 18px; margin-right: 12px; cursor: pointer; accent-color: #8b5cf6; }";
    html += ".file-info { flex: 1; display: flex; align-items: center; }";
    html += ".file-name { font-size: 16px; font-weight: 600; color: #1f2937; margin-bottom: 5px; cursor: pointer; }";
    html += ".file-name:hover { color: #8b5cf6; }";
    html += ".file-meta { font-size: 13px; color: #6b7280; }";
    html += ".folder-item { background: #f0f9ff; padding: 15px 20px; border-radius: 12px; border: 2px solid #0ea5e9; display: flex; justify-content: space-between; align-items: center; transition: all 0.3s; margin-bottom: 8px; cursor: pointer; }";
    html += ".folder-item:hover { background: #e0f2fe; transform: translateX(4px); box-shadow: 0 4px 12px rgba(14, 165, 233, 0.2); }";
    html += ".folder-icon { font-size: 24px; margin-right: 12px; }";
    html += ".action-buttons { display: flex; gap: 10px; margin-top: 15px; }";
    html += ".toolbar { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; margin: 12px 0 18px; }";
    html += ".toolbar .btn { padding: 10px 18px; }";
    html += ".code-pill { font-family: monospace; background: #111827; color: #e5e7eb; padding: 2px 6px; border-radius: 6px; }";
    html += ".info-row { display: flex; flex-wrap: wrap; gap: 12px; margin-top: 10px; }";
    html += ".chip { background: #eef2ff; color: #3730a3; border: 1px solid #c7d2fe; padding: 6px 10px; border-radius: 999px; font-size: 12px; font-weight: 600; }";
    html += ".btn-danger { background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%); color: #ffffff; box-shadow: 0 4px 6px rgba(239, 68, 68, 0.4); }";
    html += ".btn-danger:hover { background: linear-gradient(135deg, #dc2626 0%, #b91c1c 100%); transform: translateY(-2px); box-shadow: 0 8px 16px rgba(239, 68, 68, 0.5); }";
    html += ".btn-danger:disabled { background: #9ca3af; cursor: not-allowed; opacity: 0.6; }";
     html += ".btn { padding: 12px 28px; border-radius: 10px; font-size: 14px; font-weight: 600; cursor: pointer; border: none; transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1); text-decoration: none; display: inline-block; }";
     html += ".btn-primary { background: linear-gradient(135deg, #8b5cf6 0%, #7c3aed 100%); color: #ffffff; box-shadow: 0 4px 6px rgba(139, 92, 246, 0.4); }";
     html += ".btn-primary:hover { background: linear-gradient(135deg, #7c3aed 0%, #6d28d9 100%); transform: translateY(-2px); box-shadow: 0 8px 16px rgba(139, 92, 246, 0.5); }";
     html += ".btn-success { background: linear-gradient(135deg, #10b981 0%, #059669 100%); color: #ffffff; box-shadow: 0 4px 6px rgba(16, 185, 129, 0.4); }";
     html += ".btn-success:hover { background: linear-gradient(135deg, #059669 0%, #047857 100%); transform: translateY(-2px); box-shadow: 0 8px 16px rgba(16, 185, 129, 0.5); }";
     html += ".btn-secondary { background: #f3f4f6; color: #1f2937; border: 1px solid #d1d5db; }";
     html += ".btn-secondary:hover { background: #e5e7eb; transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.1); }";
     html += ".status-indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-right: 10px; }";
     html += ".status-online { background: #10b981; box-shadow: 0 0 12px rgba(16, 185, 129, 0.8), 0 0 6px rgba(16, 185, 129, 0.4); animation: pulse 2s infinite; }";
     html += ".status-offline { background: #ef4444; box-shadow: 0 0 8px rgba(239, 68, 68, 0.5); }";
     html += "h2 { color: #1f2937; margin-bottom: 20px; }";
     html += "p.empty { color: #9ca3af; text-align: center; padding: 40px; font-style: italic; font-size: 15px; }";
    html += "@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }";
    html += "@keyframes slideIn { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }";
    html += ".pulse { animation: pulse 2s infinite; }";
    html += "</style>";
    html += "<script>";
    html += "function indicator(ok) { return ok ? '<span class=\\'status-indicator status-online\\'></span>' : '<span class=\\'status-indicator status-offline\\'></span>'; }";
    html += "function setCard(id, state, valueHtml, descText) {";
    html += "  var card = document.getElementById(id + 'Card');";
    html += "  var val = document.getElementById(id + 'Value');";
    html += "  var desc = document.getElementById(id + 'Desc');";
    html += "  if (card) card.className = 'stat-card ' + state;";
    html += "  if (val) val.innerHTML = valueHtml;";
    html += "  if (desc) desc.textContent = descText;";
    html += "}";
    html += "function refreshStatus() {";
    html += "  fetch('/status').then(r => r.json()).then(data => {";
    html += "    setCard('can', data.canInitialized ? 'ok' : 'error', indicator(data.canInitialized) + (data.canInitialized ? 'Connected' : 'Offline'), data.canInitialized ? 'Receiving messages' : 'Not initialized');";
    html += "    setCard('sd', data.sdCardReady ? 'ok' : 'error', indicator(data.sdCardReady) + (data.sdCardReady ? 'Ready' : 'Error'), data.sdCardReady ? 'SD Card active' : 'No SD Card');";
    html += "    setCard('rtc', data.rtcAvailable ? 'ok' : 'warning', indicator(data.rtcAvailable) + (data.rtcAvailable ? 'Synced' : 'N/A'), data.rtcAvailable ? 'Time synchronized' : 'Using system time');";
    html += "    setCard('wifi', data.wifiConnected ? 'ok' : 'warning', indicator(data.wifiConnected) + (data.wifiConnected ? 'STA+AP' : 'AP Only'), data.wifiConnected ? 'Dual mode active' : 'Access Point only');";
    html += "    var otaDesc = data.otaReady ? ('Host: ' + data.otaHostname + ' | Port: ' + data.otaPort) : 'OTA not initialized';";
    html += "    setCard('ota', data.otaReady ? 'ok' : 'warning', indicator(data.otaReady) + (data.otaReady ? 'Ready' : 'Disabled'), otaDesc);";
    html += "    var imuDesc = data.imuAvailable ? (data.imuHasData ? 'Streaming' : 'No data yet') : 'Not detected';";
    html += "    setCard('imu', data.imuAvailable ? 'ok' : 'warning', indicator(data.imuAvailable) + (data.imuAvailable ? 'Ready' : 'Not Ready'), imuDesc);";
    html += "    var gpsDesc = data.gpsInitialized ? (data.gpsHasFix ? 'Fix OK' : 'No fix') : 'Not initialized';";
    html += "    setCard('gps', data.gpsInitialized ? 'ok' : 'warning', indicator(data.gpsInitialized) + (data.gpsInitialized ? 'Ready' : 'Not Ready'), gpsDesc);";
    html += "    var msgCard = document.getElementById('msgCard');";
    html += "    var msgVal = document.getElementById('msgValue');";
    html += "    var msgDesc = document.getElementById('msgDesc');";
    html += "    if (msgCard) msgCard.className = 'stat-card ' + (data.messageCount > 0 ? 'ok' : 'warning');";
    html += "    if (msgVal) msgVal.textContent = data.messageCount;";
    html += "    if (msgDesc) msgDesc.textContent = data.dataTransferActive ? 'Transferring...' : 'Idle';";
    html += "  }).catch(e => { console.error('Status error:', e); });";
    html += "}";
    html += "setInterval(refreshStatus, 1000);";
    html += "refreshStatus();";
    html += "window.selectAllFiles = function() {";
    html += "  var checkboxes = document.querySelectorAll('input.file-checkbox[type=\"checkbox\"]');";
    html += "  if (checkboxes.length === 0) {";
    html += "    alert('No files to select!');";
    html += "    return;";
    html += "  }";
    html += "  for (var i = 0; i < checkboxes.length; i++) {";
    html += "    if (!checkboxes[i].disabled) {";
    html += "      checkboxes[i].checked = true;";
    html += "    }";
    html += "  }";
    html += "  updateDeleteButtonState();";
    html += "};";
    html += "window.deselectAllFiles = function() {";
    html += "  var checkboxes = document.querySelectorAll('input.file-checkbox[type=\"checkbox\"]');";
    html += "  for (var i = 0; i < checkboxes.length; i++) {";
    html += "    checkboxes[i].checked = false;";
    html += "  }";
    html += "  updateDeleteButtonState();";
    html += "};";
    html += "window.updateDeleteButtonState = function() {";
    html += "  var checked = document.querySelectorAll('input.file-checkbox[type=\"checkbox\"]:checked');";
    html += "  var btn = document.getElementById('deleteBtn');";
    html += "  if (btn) {";
    html += "    btn.disabled = checked.length === 0;";
    html += "    if (checked.length > 0) {";
    html += "      btn.textContent = '🗑 Delete Selected (' + checked.length + ')';";
    html += "    } else {";
    html += "      btn.textContent = '🗑 Delete Selected';";
    html += "    }";
    html += "    console.log('Delete button state: ' + (checked.length > 0 ? 'enabled' : 'disabled') + ' (' + checked.length + ' selected)');";
    html += "  }";
    html += "  var checkboxes = document.querySelectorAll('input.file-checkbox[type=\"checkbox\"]');";
    html += "  for (var i = 0; i < checkboxes.length; i++) {";
    html += "    var item = checkboxes[i].closest('.file-item');";
    html += "    if (item) {";
    html += "      if (checkboxes[i].checked) {";
    html += "        item.classList.add('selected');";
    html += "      } else {";
    html += "        item.classList.remove('selected');";
    html += "      }";
    html += "    }";
    html += "  }";
    html += "};";
    html += "window.deleteSelectedFiles = function() {";
    html += "  console.log('Delete Selected clicked');";
    html += "  var checked = document.querySelectorAll('input.file-checkbox[type=\"checkbox\"]:checked');";
    html += "  console.log('Checked files: ' + checked.length);";
    html += "  if (checked.length === 0) {";
    html += "    alert('No files selected! Please select files to delete.');";
    html += "    return;";
    html += "  }";
    html += "  var fileList = '';";
    html += "  for (var i = 0; i < Math.min(checked.length, 5); i++) {";
    html += "    var fileName = checked[i].value.split('/').pop();";
    html += "    fileList += '\\n- ' + fileName;";
    html += "  }";
    html += "  if (checked.length > 5) {";
    html += "    fileList += '\\n... and ' + (checked.length - 5) + ' more';";
    html += "  }";
    html += "  if (!confirm('Delete ' + checked.length + ' file(s)? This cannot be undone!' + fileList)) {";
    html += "    return;";
    html += "  }";
    html += "  var files = [];";
    html += "  for (var i = 0; i < checked.length; i++) {";
    html += "    files.push(checked[i].value);";
    html += "    console.log('File to delete: ' + checked[i].value);";
    html += "  }";
    html += "  var xhr = new XMLHttpRequest();";
    html += "  xhr.open('POST', '/delete', true);";
    html += "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');";
    html += "  xhr.onreadystatechange = function() {";
    html += "    if (xhr.readyState === 4) {";
    html += "      console.log('Response status: ' + xhr.status);";
    html += "      console.log('Response text: ' + xhr.responseText);";
    html += "      if (xhr.status === 200) {";
    html += "        try {";
    html += "          var data = JSON.parse(xhr.responseText);";
    html += "          if (data.success) {";
    html += "            var msg = 'Deleted ' + data.deleted + ' file(s) successfully';";
    html += "            if (data.failed > 0) {";
    html += "              msg += '\\n' + data.failed + ' file(s) could not be deleted';";
    html += "            }";
    html += "            alert(msg);";
    html += "            location.reload();";
    html += "          } else {";
    html += "            alert('Error: ' + (data.error || 'Failed to delete files'));";
    html += "          }";
    html += "        } catch(e) {";
    html += "          alert('Error parsing response: ' + e + '\\nResponse: ' + xhr.responseText);";
    html += "        }";
    html += "      } else {";
    html += "        alert('Error: HTTP ' + xhr.status + '\\nResponse: ' + xhr.responseText);";
    html += "      }";
    html += "    }";
    html += "  };";
    html += "  var jsonData = JSON.stringify({ files: files });";
    html += "  console.log('Sending delete request for ' + files.length + ' files');";
    html += "  console.log('Data: ' + jsonData);";
    html += "  xhr.send('plain=' + encodeURIComponent(jsonData));";
    html += "};";
    html += "function initFileSelection() {";
    html += "  console.log('Initializing file selection...');";
    html += "  var checkboxes = document.querySelectorAll('input.file-checkbox[type=\"checkbox\"]');";
    html += "  console.log('Found ' + checkboxes.length + ' checkboxes');";
    html += "  for (var i = 0; i < checkboxes.length; i++) {";
    html += "    checkboxes[i].addEventListener('change', function() {";
    html += "      updateDeleteButtonState();";
    html += "    });";
    html += "  }";
  html += "  updateDeleteButtonState();";
  html += "  console.log('File selection initialized');";
  html += "};";
    html += "window.addEventListener('load', function() {";
    html += "  console.log('Page loaded, initializing file selection');";
    html += "  initFileSelection();";
    html += "});";
    html += "if (document.readyState === 'loading') {";
    html += "  document.addEventListener('DOMContentLoaded', function() {";
    html += "    console.log('DOMContentLoaded, initializing file selection');";
    html += "    initFileSelection();";
    html += "  });";
    html += "} else {";
    html += "  console.log('Document already ready, initializing file selection');";
    html += "  initFileSelection();";
    html += "}";
    html += "@keyframes slideIn { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }";
    html += "</script>";
     html += "</head><body>";
     html += "<div class='container'>";
     html += "<div class='header'>";
     html += "<h1>🚀 CAN Data Logger</h1>";
     html += "<p>Advanced Control Panel & Monitoring System</p>";
     html += "</div>";
     
     html += "<div class='stats-grid'>";
    html += "<div class='stat-card " + String(canInitialized ? "ok" : "error") + "' id='canCard'>";
    html += "<div class='stat-label'>CAN Bus</div>";
    html += "<div class='stat-value' id='canValue'>" + String(canInitialized ? "<span class='status-indicator status-online'></span>Connected" : "<span class='status-indicator status-offline'></span>Offline") + "</div>";
    html += "<div class='stat-desc' id='canDesc'>" + String(canInitialized ? "Receiving messages" : "Not initialized") + "</div>";
    html += "</div>";
    
    html += "<div class='stat-card " + String(sdCardReady ? "ok" : "error") + "' id='sdCard'>";
    html += "<div class='stat-label'>Storage</div>";
    html += "<div class='stat-value' id='sdValue'>" + String(sdCardReady ? "<span class='status-indicator status-online'></span>Ready" : "<span class='status-indicator status-offline'></span>Error") + "</div>";
    html += "<div class='stat-desc' id='sdDesc'>" + String(sdCardReady ? "SD Card active" : "No SD Card") + "</div>";
    html += "</div>";
    
    html += "<div class='stat-card " + String(rtcAvailable ? "ok" : "warning") + "' id='rtcCard'>";
    html += "<div class='stat-label'>RTC Clock</div>";
    html += "<div class='stat-value' id='rtcValue'>" + String(rtcAvailable ? "<span class='status-indicator status-online'></span>Synced" : "<span class='status-indicator status-offline'></span>N/A") + "</div>";
    html += "<div class='stat-desc' id='rtcDesc'>" + String(rtcAvailable ? "Time synchronized" : "Using system time") + "</div>";
    html += "</div>";

    html += "<div class='stat-card " + String(imuAvailable ? "ok" : "warning") + "' id='imuCard'>";
    html += "<div class='stat-label'>IMU (ADXL345)</div>";
    html += "<div class='stat-value' id='imuValue'>" + String(imuAvailable ? "<span class='status-indicator status-online'></span>Ready" : "<span class='status-indicator status-offline'></span>Not Ready") + "</div>";
    html += "<div class='stat-desc' id='imuDesc'>" + String(imuAvailable ? (imuHasData ? "Streaming" : "No data yet") : "Not detected") + "</div>";
    html += "</div>";

    html += "<div class='stat-card " + String(gpsInitialized ? "ok" : "warning") + "' id='gpsCard'>";
    html += "<div class='stat-label'>GPS (NEO-M8N)</div>";
    html += "<div class='stat-value' id='gpsValue'>" + String(gpsInitialized ? "<span class='status-indicator status-online'></span>Ready" : "<span class='status-indicator status-offline'></span>Not Ready") + "</div>";
    html += "<div class='stat-desc' id='gpsDesc'>" + String(gpsInitialized ? (gpsHasFix ? "Fix OK" : "No fix") : "Not initialized") + "</div>";
    html += "</div>";
    
    html += "<div class='stat-card " + String(messageCount > 0 ? "ok" : "warning") + "' id='msgCard'>";
    html += "<div class='stat-label'>Messages Logged</div>";
    html += "<div class='stat-value' id='msgValue'>" + String(messageCount) + "</div>";
    html += "<div class='stat-desc' id='msgDesc'>" + String(dataTransferActive ? "Transferring..." : "Idle") + "</div>";
    html += "</div>";
    
    html += "<div class='stat-card " + String(wifiConnected ? "ok" : "warning") + "' id='wifiCard'>";
    html += "<div class='stat-label'>WiFi Status</div>";
    html += "<div class='stat-value' id='wifiValue'>" + String(wifiConnected ? "<span class='status-indicator status-online'></span>STA+AP" : "<span class='status-indicator status-offline'></span>AP Only") + "</div>";
    html += "<div class='stat-desc' id='wifiDesc'>" + String(wifiConnected ? "Dual mode active" : "Access Point only") + "</div>";
    html += "</div>";

    html += "<div class='stat-card " + String(otaReady ? "ok" : "warning") + "' id='otaCard'>";
    html += "<div class='stat-label'>OTA Update</div>";
    html += "<div class='stat-value' id='otaValue'>" + String(otaReady ? "<span class='status-indicator status-online'></span>Ready" : "<span class='status-indicator status-offline'></span>Disabled") + "</div>";
    html += "<div class='stat-desc' id='otaDesc'>";
    #if ENABLE_OTA
    html += "Host: " + String(OTA_HOSTNAME) + " | Port: " + String(OTA_PORT);
    #else
    html += "OTA disabled";
    #endif
    html += "</div>";
    html += "</div>";
     
     html += "<div class='stat-card ok'>";
     html += "<div class='stat-label'>Web Server</div>";
     html += "<div class='stat-value'><span class='status-indicator status-online'></span>Online</div>";
     html += "<div class='stat-desc'>" + WiFi.softAPIP().toString() + "</div>";
     html += "</div>";
     html += "</div>";
     
    html += "<div class='section'>";
    html += "<div class='section-title'>📁 Data Files <button class='btn btn-secondary' style='margin-left: 15px; padding: 8px 16px; font-size: 12px;' onclick='location.reload()'>🔄 Refresh</button></div>";
    
    // Get current folder from query parameter
    String currentFolder = "/CAN_Logged_Data";
    if (server.hasArg("folder")) {
        currentFolder = server.arg("folder");
        if (!currentFolder.startsWith("/")) {
            currentFolder = "/" + currentFolder;
        }
    }
    if (currentFolder.indexOf("..") >= 0) {
        currentFolder = "/CAN_Logged_Data";
    }
    
    html += "<div style='margin-bottom: 15px; padding: 12px; background: #f9fafb; border-radius: 8px; font-size: 14px;'>";
    html += "<strong>Current Folder:</strong> <span style='color: #8b5cf6; font-family: monospace;'>" + currentFolder + "</span>";
    if (currentFolder != "/") {
        String parentFolder = currentFolder.substring(0, currentFolder.lastIndexOf('/'));
        if (parentFolder.length() == 0) parentFolder = "/";
        html += " | <a href='/?folder=" + parentFolder + "' style='color: #8b5cf6; text-decoration: none;'>⬆ Parent</a>";
    }
    html += "</div>";

    String latestName = "";
    if (sdCardReady && SD.exists(currentFolder)) {
        File scanFolder = SD.open(currentFolder);
        if (scanFolder) {
            File scanEntry = scanFolder.openNextFile();
            while (scanEntry) {
                if (!scanEntry.isDirectory() && isDataFileName(String(scanEntry.name()))) {
                    String scanName = String(scanEntry.name());
                    if (scanName.lastIndexOf('/') >= 0) {
                        scanName = scanName.substring(scanName.lastIndexOf('/') + 1);
                    }
                    if (latestName.length() == 0 || scanName > latestName) {
                        latestName = scanName;
                    }
                }
                scanEntry.close();
                scanEntry = scanFolder.openNextFile();
            }
            scanFolder.close();
        }
    }
    
    html += "<div class='toolbar'>";
    html += "<button class='btn btn-secondary' onclick='selectAllFiles()'>Select All</button>";
    html += "<button class='btn btn-secondary' onclick='deselectAllFiles()'>Deselect All</button>";
    html += "<button class='btn btn-danger' id='deleteBtn' onclick='deleteSelectedFiles()' disabled>Delete Selected</button>";
    html += "</div>";

    html += "<div class='file-list-container'>";
    html += "<div class='file-list'>";
    
    int canFileCount = 0;
    int folderCount = 0;
    unsigned long totalSize = 0;
    
    // Check if folder exists
    if (sdCardReady) {
        // Ensure directory entry and size for the active log file are up to date
        if (logFile) {
            logFile.flush();
            currentFileSize = logFile.size();
        }
        if (SD.exists(currentFolder)) {
            File folder = SD.open(currentFolder);
            if (folder) {
                File entry = folder.openNextFile();
                while(entry && (canFileCount + folderCount) < 200) {
                    String entryName = String(entry.name());
                    String displayName = entryName;
                    
                    // Extract just the name part
                    if (entryName.lastIndexOf('/') >= 0) {
                        displayName = entryName.substring(entryName.lastIndexOf('/') + 1);
                    }
                    
                    if(entry.isDirectory()) {
                        // Show folder
                        String folderPath = currentFolder;
                        if (!folderPath.endsWith("/")) folderPath += "/";
                        folderPath += displayName;
                        html += "<div class='folder-item' onclick=\"location.href='/?folder=" + folderPath + "'\">";
                        html += "<div class='file-info'>";
                        html += "<span class='folder-icon'>📁</span>";
                        html += "<div>";
                        html += "<div class='file-name'>" + displayName + "</div>";
                        html += "<div class='file-meta'>Folder</div>";
                        html += "</div>";
                        html += "</div>";
                        html += "<span style='color: #0ea5e9;'>▶</span>";
                        html += "</div>";
                        folderCount++;
                    } else if (isDataFileName(String(entry.name()))) {
                        // Show file
                        totalSize += entry.size();
                        
                        // Format file size
                        String sizeStr = String(entry.size()) + " bytes";
                        if (entry.size() > 1024) {
                            sizeStr = String(entry.size() / 1024.0, 1) + " KB";
                        }
                        if (entry.size() > 1024 * 1024) {
                            sizeStr = String(entry.size() / (1024.0 * 1024.0), 2) + " MB";
                        }
                        
                        String fullPath = currentFolder;
                        if (!fullPath.endsWith("/")) fullPath += "/";
                        fullPath += displayName;
                        String fileId = "file_" + String(canFileCount);
                        
                        // Extract date and time from filename (CAN_LOG_YYYYMMDD_HHMMSS.NXT)
                        String fileDate = "Unknown";
                        String fileTime = "Unknown";
                        if (displayName.startsWith("CAN_LOG_") && displayName.length() >= 21) {
                            // Format: CAN_LOG_YYYYMMDD_HHMMSS.NXT
                            String datePart = displayName.substring(8, 16);  // YYYYMMDD
                            String timePart = displayName.substring(17, 23);  // HHMMSS
                            
                            if (datePart.length() == 8 && timePart.length() == 6) {
                                // Format: YYYY-MM-DD
                                fileDate = datePart.substring(0, 4) + "-" + datePart.substring(4, 6) + "-" + datePart.substring(6, 8);
                                // Format: HH:MM:SS
                                fileTime = timePart.substring(0, 2) + ":" + timePart.substring(2, 4) + ":" + timePart.substring(4, 6);
                            }
                        }
                        
                        String displayLower = displayName;
                        displayLower.toLowerCase();
                        String currentLower = currentLogFileName;
                        currentLower.toLowerCase();
                        bool isActiveFile = (currentLower.endsWith(displayLower) && displayLower.length() > 0);
                        bool isLatestFile = (latestName.length() > 0 && displayName == latestName);

                        String itemClass = "file-item";
                        if (isActiveFile) {
                            itemClass += " active";
                        }

                        html += "<div class='" + itemClass + "' id='" + fileId + "'>";
                        html += "<div class='file-info'>";
                        String checkboxAttr = isActiveFile ? " disabled title='Active file cannot be deleted'" : "";
                        html += "<input type='checkbox' class='file-checkbox' value='" + fullPath + "' onchange='updateDeleteButtonState()'" + checkboxAttr + ">";
                        html += "<div style='flex: 1;'>";
                        html += "<div class='file-name'>";
                        if (isActiveFile) {
                            html += "<span class='badge badge-active'>ACTIVE</span>";
                        }
                        if (isLatestFile) {
                            html += "<span class='badge badge-latest'>LATEST</span>";
                        }
                        html += displayName + "</div>";
                        html += "<div class='file-meta'>Size: " + sizeStr;
                        html += " | Date: " + fileDate + " | Time: " + fileTime;
                        html += "</div>";
                        html += "</div>";
                        html += "</div>";
                        html += "<a href='/download?file=" + fullPath + "' class='btn btn-success' style='margin-right: 8px;' onclick='event.stopPropagation()'>Download</a>";
                        html += "</div>";
                        canFileCount++;
                    }
                    entry.close();
                    entry = folder.openNextFile();
                }
                folder.close();
            }
        }
    }
    
    html += "</div></div>";
    
    if(canFileCount == 0 && folderCount == 0) {
        html += "<p class='empty'>No files or folders available in this directory.</p>";
        if (!sdCardReady) {
            html += "<p class='empty' style='color: #ef4444;'>⚠️ SD Card not ready!</p>";
        }
    } else {
        String totalSizeStr = String(totalSize) + " bytes";
        if (totalSize > 1024) {
            totalSizeStr = String(totalSize / 1024.0, 1) + " KB";
        }
        if (totalSize > 1024 * 1024) {
            totalSizeStr = String(totalSize / (1024.0 * 1024.0), 2) + " MB";
        }
        html += "<div style='margin-top: 20px; padding: 18px; background: #f9fafb; border-radius: 12px; border: 1px solid #e5e7eb; box-shadow: 0 1px 2px rgba(0,0,0,0.05);'>";
        html += "<strong style='color: #8b5cf6;'>Files:</strong> <span style='color: #1f2937;'>" + String(canFileCount) + "</span>";
        html += " | <strong style='color: #8b5cf6;'>Folders:</strong> <span style='color: #1f2937;'>" + String(folderCount) + "</span>";
        html += " | <strong style='color: #8b5cf6;'>Total Size:</strong> <span style='color: #1f2937;'>" + totalSizeStr + "</span>";
        if (sdCardReady && currentLogFileName.length() > 0) {
            String currentFile = currentLogFileName.substring(currentLogFileName.lastIndexOf('/') + 1);
            html += " | <strong style='color: #8b5cf6;'>Current File:</strong> <span style='color: #1f2937;'>" + currentFile + "</span>";
            html += " (" + String(currentFileSize) + " bytes)";
        }
        html += "</div>";
    }
    html += "</div>";

    html += "<div class='section'>";
    html += "<div class='section-title'>OTA Update</div>";
    html += "<p>OTA lets you upload new firmware wirelessly from the Arduino IDE once the logger is on WiFi.</p>";
    html += "<div class='info-row'>";
    html += "<div class='chip'>AP IP: " + WiFi.softAPIP().toString() + "</div>";
    if (WiFi.status() == WL_CONNECTED) {
        html += "<div class='chip'>STA IP: " + WiFi.localIP().toString() + "</div>";
    } else {
        html += "<div class='chip'>STA IP: not connected</div>";
    }
    #if ENABLE_OTA
    html += "<div class='chip'>Hostname: " + String(OTA_HOSTNAME) + "</div>";
    html += "<div class='chip'>Port: " + String(OTA_PORT) + "</div>";
    #else
    html += "<div class='chip'>OTA disabled</div>";
    #endif
    html += "</div>";
    html += "<div style='margin-top: 12px; font-size: 14px; color: #374151; line-height: 1.5;'>";
    html += "<strong>Steps:</strong> 1) Connect your PC to the logger WiFi. 2) Open Arduino IDE. 3) Tools -> Port -> Network Ports. 4) Select the device and click Upload.";
    html += "</div>";
    html += "</div>";
     
     html += "</div></body></html>";
     
     server.sendHeader("Cache-Control", "no-store");
     server.send(200, "text/html", html);
 }
 
void handleFileList() {
    String json = "[";
    bool first = true;
    
    if (sdCardReady) {
        // Check CAN_Logged_Data folder first
        if (SD.exists("/CAN_Logged_Data")) {
            File folder = SD.open("/CAN_Logged_Data");
            if (folder) {
                File file = folder.openNextFile();
                while (file) {
                    if (!file.isDirectory() && isDataFileName(String(file.name()))) {
                        if (!first) json += ",";
                        first = false;
                        String fileName = String(file.name());
                        // Remove leading path
                        if (fileName.startsWith("/CAN_Logged_Data/")) {
                            fileName = fileName.substring(18);
                        } else if (fileName.startsWith("CAN_Logged_Data/")) {
                            fileName = fileName.substring(16);
                        }
                        json += "{\"name\":\"CAN_Logged_Data/" + fileName + "\",\"size\":" + String(file.size()) + "}";
                    }
                    file.close();
                    file = folder.openNextFile();
                }
                folder.close();
            }
        }
        
        // Also check root for backward compatibility
        File root = SD.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory() && isDataFileName(String(file.name()))) {
                    // Skip if already in CAN_Logged_Data
                    String fileName = String(file.name());
                    if (fileName.startsWith("/")) {
                        fileName = fileName.substring(1);
                    }
                    if (!fileName.startsWith("CAN_Logged_Data/")) {
                        if (!first) json += ",";
                        first = false;
                        json += "{\"name\":\"" + fileName + "\",\"size\":" + String(file.size()) + "}";
                    }
                }
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }
    }
    
    json += "]";
    server.send(200, "application/json", json);
}
 
void handleFileDownload() {
    if (!sdCardReady) {
        server.send(503, "text/plain", "SD Card not available");
        return;
    }
    
    String fileName = server.arg("file");
    if (fileName.length() == 0) {
        server.send(400, "text/plain", "File name required");
        return;
    }
    
    // Security check - prevent directory traversal
    if (fileName.indexOf("..") >= 0) {
        server.send(403, "text/plain", "Invalid file name");
        return;
    }
    
    // Construct full path
    String filePath;
    if (fileName.startsWith("/")) {
        filePath = fileName;
    } else if (fileName.startsWith("CAN_Logged_Data/")) {
        filePath = "/" + fileName;
    } else {
        // Try CAN_Logged_Data folder first, then root
        filePath = "/CAN_Logged_Data/" + fileName;
        if (!SD.exists(filePath)) {
            filePath = "/" + fileName;
        }
    }
    
    // Check if requested file is the active log file
    String normalizedFilePath = filePath;
    if (!normalizedFilePath.startsWith("/")) {
        normalizedFilePath = "/" + normalizedFilePath;
    }
    String currentPath = currentLogFileName;
    if (!currentPath.startsWith("/")) {
        currentPath = "/" + currentPath;
    }
    bool isActiveFile = (currentLogFileName.length() > 0 && normalizedFilePath == currentPath);

    if (isActiveFile && logFile) {
        logFile.flush();
    }

    bool reopenAfterDownload = false;
    File file = SD.open(filePath, FILE_READ);
    if (!file && isActiveFile && logFile) {
        // Close active file to allow read, then reopen after download
        logFile.flush();
        logFile.close();
        delay(20);
        file = SD.open(filePath, FILE_READ);
        reopenAfterDownload = true;
    }
    if (!file) {
        server.send(404, "text/plain", "File not found: " + filePath);
        return;
    }
    
    // Extract just the filename for download
    String downloadName = fileName;
    if (downloadName.lastIndexOf('/') >= 0) {
        downloadName = downloadName.substring(downloadName.lastIndexOf('/') + 1);
    }
    
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + downloadName + "\"");
    server.sendHeader("Cache-Control", "no-cache");

    // Stream manually to avoid issues with large files or active logging
    size_t totalSize = file.size();
    server.setContentLength(totalSize);
    String contentType = "application/octet-stream";
    String lowerPath = filePath;
    lowerPath.toLowerCase();
    if (lowerPath.endsWith(".csv")) {
        contentType = "text/csv";
    }
    server.send(200, contentType, "");

    WiFiClient client = server.client();
    const size_t bufSize = 1024;
    uint8_t buf[bufSize];
    size_t remaining = totalSize;
    while (client.connected() && remaining > 0) {
        size_t toRead = remaining > bufSize ? bufSize : remaining;
        size_t n = file.read(buf, toRead);
        if (n == 0) {
            break;
        }
        size_t w = client.write(buf, n);
        if (w == 0) {
            break;
        }
        remaining -= w;
        delay(0);
    }
    if (remaining > 0) {
        Serial.printf("WARNING: Download incomplete, %lu bytes not sent\n", (unsigned long)remaining);
    }
    file.close();

    if (reopenAfterDownload) {
        logFile = SD.open(currentLogFileName, FILE_WRITE);
        if (logFile) {
            logFile.seek(logFile.size());
            currentFileSize = logFile.size();
        } else {
            Serial.println("ERROR: Failed to reopen active log file after download");
            sdCardReady = false;
        }
    }
}

void handleFileDelete() {
    if (!sdCardReady) {
        server.send(503, "application/json", "{\"success\":false,\"error\":\"SD Card not available\"}");
        return;
    }
    
    // ESP32 WebServer: POST body is available via arg("plain")
    String body = server.arg("plain");
    
    // If body is empty, try to get files from URL parameter (fallback)
    if (body.length() == 0) {
        String filesParam = server.arg("files");
        if (filesParam.length() > 0) {
            // Convert URL parameter format to JSON-like format for parsing
            body = "{\"files\":[" + filesParam + "]}";
        }
    }
    
    if (body.length() == 0) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"No data provided\"}");
        return;
    }
    
    Serial.println("Delete request body: " + body);
    
    // Parse JSON (simple parsing)
    int filesStart = body.indexOf("\"files\":[");
    if (filesStart < 0) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON format\"}");
        return;
    }
    
    int deleted = 0;
    int failed = 0;
    
    // Extract file paths from JSON array
    int start = body.indexOf("[", filesStart) + 1;
    int end = body.indexOf("]", start);
    if (start > 0 && end > start) {
        String filesStr = body.substring(start, end);
        filesStr.replace("\"", "");
        filesStr.replace(" ", "");
        
        int lastPos = 0;
        while (lastPos < filesStr.length()) {
            int commaPos = filesStr.indexOf(",", lastPos);
            String filePath;
            if (commaPos > 0) {
                filePath = filesStr.substring(lastPos, commaPos);
                lastPos = commaPos + 1;
            } else {
                filePath = filesStr.substring(lastPos);
                lastPos = filesStr.length();
            }
            
            if (filePath.length() > 0) {
                // Security check
                if (filePath.indexOf("..") >= 0) {
                    failed++;
                    Serial.println("Security check failed for: " + filePath);
                    continue;
                }
                
                // Ensure path starts with /
                if (!filePath.startsWith("/")) {
                    filePath = "/" + filePath;
                }
                
                // Don't allow deleting current log file
                if (filePath == currentLogFileName) {
                    failed++;
                    Serial.println("Cannot delete active log file: " + filePath);
                    continue;
                }
                
                // Try to delete file
                if (SD.exists(filePath)) {
                    if (SD.remove(filePath)) {
                        deleted++;
                        Serial.println("Successfully deleted file: " + filePath);
                    } else {
                        failed++;
                        Serial.println("Failed to delete file (remove failed): " + filePath);
                    }
                } else {
                    failed++;
                    Serial.println("File not found: " + filePath);
                }
            }
        }
    } else {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Could not parse file list\"}");
        return;
    }
    
    String response = "{\"success\":true,\"deleted\":" + String(deleted) + ",\"failed\":" + String(failed) + "}";
    Serial.println("Delete response: " + response);
    server.send(200, "application/json", response);
}

void handleFolderBrowse() {
    String folderPath = server.arg("path");
    if (folderPath.length() == 0) {
        folderPath = "/CAN_Logged_Data";
    }
    
    if (!folderPath.startsWith("/")) {
        folderPath = "/" + folderPath;
    }
    
    // Security check
    if (folderPath.indexOf("..") >= 0) {
        server.send(403, "text/plain", "Invalid path");
        return;
    }
    
    if (!SD.exists(folderPath)) {
        server.send(404, "text/plain", "Folder not found");
        return;
    }
    
    String json = "[";
    bool first = true;
    
    File folder = SD.open(folderPath);
    if (folder) {
        File entry = folder.openNextFile();
        while (entry) {
            if (!first) json += ",";
            first = false;
            
            String entryName = String(entry.name());
            String displayName = entryName;
            if (entryName.lastIndexOf('/') >= 0) {
                displayName = entryName.substring(entryName.lastIndexOf('/') + 1);
            }
            
            json += "{\"name\":\"" + displayName + "\",";
            json += "\"path\":\"" + entryName + "\",";
            json += "\"isDir\":" + String(entry.isDirectory() ? "true" : "false") + ",";
            if (!entry.isDirectory()) {
                json += "\"size\":" + String(entry.size());
            } else {
                json += "\"size\":0";
            }
            json += "}";
            
            entry = folder.openNextFile();
        }
        folder.close();
    }
    
    json += "]";
    server.send(200, "application/json", json);
}

void handleLiveData() {
    uint32_t since = 0;
    if (server.hasArg("since")) {
        long sinceArg = server.arg("since").toInt();
        if (sinceArg > 0) {
            since = (uint32_t)sinceArg;
        }
    }
     
     int limit = LIVE_DEFAULT_LIMIT;
     if (server.hasArg("limit")) {
         int requested = server.arg("limit").toInt();
         if (requested > 0) {
             limit = constrain(requested, 1, LIVE_BUFFER_SIZE);
         }
     }

     String json;
     json.reserve(8192);
     json += "{\"status\":\"ok\",\"latest\":";
     json += String(liveFrameCounter);
     json += ",\"frames\":[";
     
     bool first = true;
     if (liveFrameCounter > 0) {
         uint32_t earliestSeq = (liveFrameCounter >= LIVE_BUFFER_SIZE) ? (liveFrameCounter - LIVE_BUFFER_SIZE + 1) : 1;
         uint32_t startSeq = (since > 0) ? since + 1 : earliestSeq;
         if (startSeq < earliestSeq) {
             startSeq = earliestSeq;
         }
         if (startSeq > liveFrameCounter) {
             startSeq = liveFrameCounter;
         }
         
         int added = 0;
         for (uint32_t seq = startSeq; seq <= liveFrameCounter && added < limit; seq++) {
             size_t idx = (seq - 1) % LIVE_BUFFER_SIZE;
             LiveFrame &frame = liveFrameBuffer[idx];
             if (frame.seq != seq) {
                 continue;
             }
             
             if (!first) {
                 json += ",";
             }
             first = false;
             added++;
             
             json += "{\"seq\":";
             json += String(frame.seq);
             json += ",\"time\":\"";
             if (strlen(frame.timeText) > 0) {
                 json += frame.timeText;
             }
             json += "\",\"unix\":";
             json += String((unsigned long)frame.unixTime);
             json += ",\"micros\":";
             json += String((unsigned long)frame.micros);
             json += ",\"id\":\"";
             String idStr = String(frame.identifier, HEX);
             idStr.toUpperCase();
             json += idStr;
             json += "\",\"extended\":";
             json += frame.extended ? "true" : "false";
             json += ",\"rtr\":";
             json += frame.rtr ? "true" : "false";
             json += ",\"dlc\":";
             json += String(frame.dlc);
             json += ",\"data\":[";
             for (int i = 0; i < frame.dlc && i < 8; i++) {
                 if (i > 0) {
                     json += ",";
                 }
                 char hexByte[3];
                 snprintf(hexByte, sizeof(hexByte), "%02X", frame.data[i]);
                 json += "\"";
                 json += hexByte;
                 json += "\"";
             }
             json += "]}";
         }
     }
     
     json += "]}";
     server.send(200, "application/json", json);
 }
 #endif
 
 // ==================== SETUP ====================
 void setup() {
     // CRITICAL: Serial MUST be first, and prints MUST happen immediately
     // NO DELAYS before first print!
     Serial.begin(115200);
     
     // IMMEDIATE print - happens BEFORE anything else
     Serial.print("\n\n\n");
     Serial.print("========================================\n");
     Serial.print("=== CAN DATA LOGGER - ESP32-C6 PICO ===\n");
     Serial.print("========================================\n");
     Serial.print(">>> CODE STARTED - SERIAL IS WORKING <<<\n");
     delay(500);
     
     // Clear buffer
     while(Serial.available()) {
         Serial.read();
     }
     
     Serial.print("Baud rate: 115200\n");
     Serial.print("Starting initialization...\n");
     delay(100);
     yield();
     
     // Initialize RGB LED
     Serial.println("Step 1: Initializing Neopixel LED...");
     pixels.begin();
     pixels.setBrightness(50);
     setLED(LED_BLUE);
     Serial.println("LED initialized - Status: BLUE (System Initializing)");
     yield();
     
    // Record boot time in milliseconds
    bootMillis = millis();
    Serial.print("Boot time: ");
     Serial.print(millis());
     Serial.println(" ms");
     yield();

     // Initialize shared I2C bus (RTC + IMU)
     Serial.println("Step 1.5: Initializing shared I2C bus...");
     initializeI2CBus();
     
     // ===== STEP 2: Initialize RTC =====
     Serial.println("Step 2: Initializing RTC...");
     rtcAvailable = initializeRTC();
     yield();
     
     // Read time from RTC if available
     if (rtcAvailable) {
         baseTime = getRTCTime();
         bootMillis = millis();  // Reset boot reference
         Serial.println("RTC time read successfully");
         Serial.println("RTC Status: CONNECTED");
     } else {
         Serial.println("WARNING: RTC not available - will use compile time");
         Serial.println("RTC Status: NOT CONNECTED");
     }

    // ===== STEP 2.5: Initialize IMU (ADXL345) =====
    Serial.println("Step 2.5: Initializing IMU (ADXL345)...");
    imuAvailable = initializeIMU();
    Serial.println(imuAvailable ? "IMU Status: CONNECTED" : "IMU Status: NOT DETECTED");
     
     // ===== STEP 3: Initialize WiFi FIRST (before SD card to start web server quickly) =====
     #if ENABLE_WIFI
     Serial.println("Step 3: Initializing WiFi...");
     if (initializeWiFi()) {
         Serial.println("WiFi initialized");
         
        // Setup web server routes IMMEDIATELY after WiFi is ready
        server.on("/", handleRoot);
        server.on("/files", handleFileList);
        server.on("/download", handleFileDownload);
        server.on("/delete", HTTP_POST, handleFileDelete);
        server.on("/folder", handleFolderBrowse);
        server.on("/status", handleStatus);
        server.on("/live", handleLiveData);
        server.onNotFound([]() {
            server.send(404, "text/plain", "File not found");
        });
         
         // Start web server IMMEDIATELY
         server.begin();
         Serial.println("========================================");
         Serial.println("Web server started on http://192.168.10.1");
         Serial.println("Web server is READY - you can access it now!");
         Serial.println("========================================");

        #if ENABLE_OTA
        initializeOTA();
        #endif
         
        if (wifiConnected && !ntpSynced) {
            Serial.println("Attempting NTP sync...");
            if (syncTimeFromNTP()) {
                ntpSynced = true;
                lastNtpSyncMs = millis();
                Serial.println("NTP sync: OK");
            } else {
                Serial.println("NTP sync: FAILED (will retry in background)");
            }
        }
     }
     #endif
     
     // ===== STEP 4: Initialize SD Card =====
     Serial.println("Step 4: Initializing SD Card...");
     sdCardReady = initializeSDCard();
     yield();
     if (sdCardReady) {
         if (createNewLogFile()) {
             Serial.println("========================================");
             Serial.println("SD Card ready - logging initialized");
             Serial.printf("Logging to: %s\n", currentLogFileName.c_str());
             Serial.println("========================================");
         } else {
             Serial.println("ERROR: Failed to create log file on SD card!");
             Serial.println("SD Card Status: NOT READY");
             sdCardReady = false;
         }
     } else {
        Serial.println("ERROR: SD Card not available or authentication failed!");
        Serial.println("Check SD card:");
        Serial.println("  - Is it inserted properly?");
        Serial.println("  - Is it formatted as FAT32?");
        Serial.println("  - Is it write-protected?");
        Serial.println("  - Is it compatible with the module?");
        Serial.println("SD Card Status: NOT AVAILABLE");
        Serial.println("System will auto-detect SD card if inserted later (checks every 2 seconds)");
        Serial.println("CAN messages will be displayed but not logged until SD card is working");
        Serial.println("LED will show RED until SD card is detected and working");
    }
     
     // Web server is already running - print reminder
     #if ENABLE_WIFI
     Serial.println("========================================");
     Serial.println("Web server is running and accessible!");
     Serial.println("Open http://192.168.10.1 in your browser");
     Serial.println("========================================");
     #endif
     
    // ===== STEP 5: Initialize CAN Bus =====
    Serial.println("Step 5: Initializing CAN Bus...");
    Serial.println("CAN ID Filtering: DISABLED - All CAN messages will be logged");
    Serial.println("This ensures maximum compatibility with any CAN device");
    Serial.println();
    yield();
     
    if(!initCAN()) {
        Serial.println("ERROR: CAN Bus initialization failed!");
        Serial.println("Check wiring and connections:");
        Serial.printf("  - CAN TX: GPIO %d\n", CAN_TX_PIN);
        Serial.printf("  - CAN RX: GPIO %d\n", CAN_RX_PIN);
        Serial.println("  - CANH and CANL connected to motor controller");
        Serial.println("  - TJA1050 transceiver powered");
        Serial.println("System will continue but CAN will not work.");
        Serial.println("CAN will retry initialization every 5 seconds.");
        canInitialized = false;
        // LED will be updated by updateSystemLED() which checks all components
    } else {
        Serial.println("CAN Bus initialized successfully!");
        Serial.print("Speed: 500 kbps");
        Serial.println();
        Serial.println("Format: [YYYY-MM-DD HH:MM:SS][COUNT] ID:DATA");
        Serial.println("Waiting for CAN messages from motor controller...");
        Serial.println("----------------------------------------");
    }

    // ===== STEP 6: Initialize GPS =====
    gpsInitialized = initializeGPS();
    Serial.println(gpsInitialized ? "GPS Status: UART READY" : "GPS Status: NOT READY");
     
    if (ntpSynced) {
        Serial.println("Using NTP-synced time");
        if (rtcAvailable) {
            baseTime = getRTCTime();
            bootMillis = millis();
            Serial.println("RTC Status: CONNECTED (synced)");
        } else {
            Serial.println("RTC Status: NOT CONNECTED (NTP system time only)");
        }
    } else if (rtcAvailable) {
        baseTime = getRTCTime();
        bootMillis = millis();
        Serial.println("Using RTC for time synchronization");
        Serial.println("RTC Status: CONNECTED");
    } else {
         // Fallback: Use compile-time date/time
         Serial.println("WARNING: RTC is NOT connected - using compile-time as fallback");
         Serial.println("RTC Status: NOT CONNECTED");
         
         // Parse compile-time date and time
         const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
         
         char monthStr[4];
         int year, month, day, hour, minute, second;
         
         sscanf(__DATE__, "%s %d %d", monthStr, &day, &year);
         sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
         
         month = 1;
         for (int i = 0; i < 12; i++) {
             if (strcmp(monthStr, months[i]) == 0) {
                 month = i + 1;
                 break;
             }
         }
         
         struct tm timeStruct = {0};
         timeStruct.tm_year = year - 1900;
         timeStruct.tm_mon = month - 1;
         timeStruct.tm_mday = day;
         timeStruct.tm_hour = hour;
         timeStruct.tm_min = minute;
         timeStruct.tm_sec = second;
         
         baseTime = mktime(&timeStruct);
         Serial.printf("Using compile time: %04d-%02d-%02d %02d:%02d:%02d\n",
                      year, month, day, hour, minute, second);
     }
     
     if (baseTime != -1) {
         struct tm* timeinfo = localtime(&baseTime);
         Serial.printf("Base time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
     } else {
         Serial.println("Failed to create base time - using millis() instead");
         baseTime = 0;
     }
     
     Serial.println("========================================");
     Serial.println("System ready! CAN messages will be logged to SD card as .NXT files.");
     Serial.println("CAN messages will print here when received.");
     #if ENABLE_WIFI
     Serial.println("Web server is running at http://192.168.10.1");
     #endif
     Serial.println("========================================");
     
    // Update LED status after initialization (shows final status)
    // LED will be GREEN only if RTC, SD, WiFi STA+AP, CAN, and log file are all ready
    // Otherwise RED
    updateSystemLED();
    
    // Print final system status and LED indication
    Serial.println("========================================");
    Serial.println("=== SYSTEM INITIALIZATION COMPLETE ===");
    Serial.println("========================================");
    Serial.print("RTC: ");
    Serial.println(rtcAvailable ? "READY" : "NOT AVAILABLE");
    Serial.print("SD Card: ");
    Serial.println(sdCardReady ? "READY" : "NOT AVAILABLE");
    Serial.print("CAN Bus: ");
    Serial.println(canInitialized ? "READY" : "NOT INITIALIZED");
    
    #if ENABLE_WIFI
    bool wifiReady = wifiConnected;
    #else
    bool wifiReady = true;
    #endif
    bool logReady = sdCardReady && logFile;
    bool allReady = rtcAvailable && canInitialized && logReady && wifiReady;
    if (allReady) {
        Serial.println("========================================");
        Serial.println(">>> SYSTEM READY TO LOG DATA <<<");
        Serial.println("LED Status: GREEN - Ready to log CAN data");
        Serial.println("Connect CAN transceiver (CANH/CANL) to motor controller");
        Serial.println("Data will be logged automatically when CAN messages are received");
        Serial.println("You can connect the transceiver anytime - system is ready!");
        Serial.println("========================================");
        setLED(LED_GREEN);  // Explicitly set GREEN to indicate ready state
    } else {
        Serial.println("========================================");
        Serial.println(">>> SYSTEM NOT FULLY READY <<<");
        Serial.println("LED Status: RED - Some components not ready");
        Serial.print("Missing: ");
        if (!rtcAvailable) Serial.print("RTC ");
        if (!sdCardReady) Serial.print("SD Card ");
        if (!canInitialized) Serial.print("CAN Bus ");
        if (!wifiReady) Serial.print("WiFi STA+AP ");
        if (!logReady) Serial.print("Log File ");
        Serial.println();
        Serial.println("Check the status above and fix any issues");
        Serial.println("System will retry CAN initialization every 5 seconds");
        Serial.println("SD card will be checked every 2 seconds");
        Serial.println("========================================");
        setLED(LED_RED);
    }
     
     // Final delay to ensure Serial output is sent
     delay(100);  // Reduced delay
     yield();  // Allow web server to start processing
 }
 
 // ==================== MAIN LOOP ====================
void loop() {
    // Keep IMU data updated continuously for smooth logging
    updateIMU();
    // Update GPS data (non-blocking)
    updateGPS();
    // Handle web server requests
    #if ENABLE_WIFI
    server.handleClient();
    #if ENABLE_OTA
    ArduinoOTA.handle();
    #endif
    #endif
     
    // Process CAN messages
     if(canInitialized) {
         twai_message_t message;
         uint8_t processedCount = 0;
         const uint8_t maxProcessPerCall = 200;  // Process more frames per loop to capture all IDs
         
        // Use a small timeout (10ms) to allow other tasks to run
        while (processedCount < maxProcessPerCall && twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {
            uint32_t canId = message.identifier;
            
            // Log ALL CAN messages (no filtering)
            messageCount++;
            
            // Update last data received time
            lastDataReceivedTime = millis();
            
            // Data transfer started - show CAN ID when logging starts
            if (!dataTransferActive) {
                dataTransferActive = true;
                Serial.println("========================================");
                Serial.println(">>> DATA LOGGING STARTED <<<");
                Serial.println("========================================");
                Serial.printf("First CAN ID received: 0x%03X (DLC: %d)\n", canId, message.data_length_code);
                Serial.print("Data: ");
                for(int i = 0; i < message.data_length_code && i < 8; i++) {
                    Serial.printf("%02X ", message.data[i]);
                }
                Serial.println();
                Serial.println("========================================");
                Serial.printf("SD Card Ready: %s\n", sdCardReady ? "YES" : "NO");
                Serial.printf("Log File Open: %s\n", logFile ? "YES" : "NO");
                Serial.printf("IMU Ready: %s\n", (imuAvailable && imuHasData) ? "YES" : "NO");
                Serial.println("All received CAN messages will be logged");
                Serial.println("========================================");
            }

            // Ensure a log file exists as soon as CAN traffic starts.
            // Do NOT block logging on IMU availability; log accel values when available, otherwise zeros.
            if (sdCardReady && !logFile) {
                if (!createNewLogFile()) {
                    Serial.println("ERROR: Failed to create log file when data started!");
                    // Keep SD marked ready; we'll retry on subsequent frames / SD checks.
                    updateSystemLED();
                }
            }
                
            
            // Update LED to MAGENTA when actively logging
            setLED(LED_MAGENTA);
             
            time_t currentTimestamp = 0;
            uint32_t currentMicros = 0;
             
            if (rtcAvailable) {
                unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
                currentTimestamp = baseTime + elapsedSeconds;
                currentMicros = micros() % 1000000;
            } else if (baseTime != 0) {
                unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
                currentTimestamp = baseTime + elapsedSeconds;
                currentMicros = (millis() - bootMillis) * 1000;
            }
             
            struct tm* timeinfo = localtime(&currentTimestamp);
            char timeStr[32];
            sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
                   timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                   timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
             
            // Print timestamp and message counter with CAN ID
            // Print first 20 messages fully, then every 10th message
            if (messageCount <= 20 || messageCount % 10 == 0) {
                Serial.printf("[%s][%04lu] ", timeStr, messageCount);
                
                // Print CAN ID in hex format
                if(message.extd) {
                    // Extended frame (29-bit ID)
                    Serial.printf("%08X:", message.identifier);
                } else {
                    // Standard frame (11-bit ID)
                    Serial.printf("%03X:", message.identifier);
                }
                
                // Print data bytes in hex format
                for(int i = 0; i < message.data_length_code; i++) {
                    Serial.printf("%02X", message.data[i]);
                }
                
                // Add logging indicator
                if (sdCardReady) {
                    Serial.print(" [LOG]");
                } else {
                    Serial.print(" [NO SD]");
                }
                
                Serial.println(); // New line after each message
            } else if (messageCount == 100 || messageCount == 500 || messageCount == 1000) {
                // Print milestone messages with CAN ID summary
                Serial.printf("[%s][%04lu] Milestone: %lu messages logged\n", timeStr, messageCount, messageCount);
            }
             
            // Create CanFrame structure
            CanFrame rxFrame;
            rxFrame.identifier = message.identifier;
            rxFrame.extd = message.extd;
            rxFrame.rtr = message.rtr;
            rxFrame.data_length_code = message.data_length_code;
            memcpy(rxFrame.data, message.data, 8);
             
            storeLiveFrame(rxFrame, currentTimestamp, currentMicros, String(timeStr));
             
            // Log to SD card if SD is ready. `writeCANMessage()` will auto-create/recover the file if needed.
            if (sdCardReady) {
                if (!writeCANMessage(rxFrame, currentTimestamp, currentMicros)) {
                    Serial.printf("ERROR: Failed to log message %lu (CAN ID: 0x%03X) to SD card!\n", messageCount, canId);
                    Serial.println("Attempting to recover...");
                    // Try to recover by creating a new file
                    if (!createNewLogFile()) {
                        Serial.println("ERROR: Failed to recover SD card logging!");
                        sdCardReady = false;
                        updateSystemLED();  // Update LED to reflect SD card failure
                    } else {
                        Serial.println("Recovery successful - retrying write...");
                        // Retry writing the message
                        if (!writeCANMessage(rxFrame, currentTimestamp, currentMicros)) {
                            Serial.println("ERROR: Retry also failed! SD card may be full or corrupted.");
                            sdCardReady = false;
                            updateSystemLED();
                        } else {
                            Serial.printf("Retry successful - message %lu (CAN ID: 0x%03X) logged!\n", messageCount, canId);
                            setLED(LED_MAGENTA);
                        }
                    }
                } else {
                    // Successfully logged - ensure LED shows MAGENTA
                    setLED(LED_MAGENTA);
                }
            } else {
                // SD card not ready - still show MAGENTA if CAN is working
                // but update system LED to show SD card issue
                if (messageCount % 10 == 0) {  // Only print every 10th message to avoid spam
                    Serial.printf("WARNING: SD Card not ready - CAN ID 0x%03X received but not logged!\n", canId);
                }
            }
            
            // Periodic yield for high-throughput logging (every 5 messages)
            if (messageCount % 5 == 0) {
                yield();  // Prevent watchdog timeout during high-speed logging
            }
            processedCount++;
            
            // Yield every 10 processed messages to keep system responsive
            if (processedCount % 10 == 0) {
                yield();
            }
        }
        
        // Additional yield after processing batch to allow other tasks
        if (processedCount > 0) {
            yield();
        }
        
        // Check for data transfer timeout (no messages for 2 seconds)
        if (dataTransferActive && (millis() - lastDataReceivedTime) >= DATA_TIMEOUT_MS) {
            dataTransferActive = false;
            Serial.println("========================================");
            Serial.println("Data transfer: STOPPED (timeout - no messages for 2 seconds)");
            Serial.printf("Total messages logged: %lu\n", messageCount);
            if (sdCardReady) {
                Serial.printf("Last log file: %s (%lu bytes)\n", currentLogFileName.c_str(), currentFileSize);
            }
            Serial.println("LED will show ORANGE if all systems ready, or RED if any component failed");
            Serial.println("System is still ready to log data when CAN messages arrive");
            Serial.println("========================================");
            updateSystemLED();  // Will show ORANGE if all systems ready, or RED if any failed
        } else if (!dataTransferActive && messageCount == 0) {
            // No messages received yet - reflect current system readiness
            updateSystemLED();
        }
    } else {
        // CAN bus not initialized
        if (dataTransferActive) {
            dataTransferActive = false;
            Serial.println("Data transfer: STOPPED - CAN bus not initialized");
        }
        updateSystemLED();  // Will show RED since CAN is not initialized
    }
    
    // Periodic status update and LED check
    static unsigned long lastStatusUpdate = 0;
    unsigned long currentTime = millis();
    if(currentTime - lastStatusUpdate >= 10000) {  // Every 10 seconds
        // Update LED based on current system status
        updateSystemLED();
        
        // Print status
        Serial.println("=== System Status ===");
        Serial.print("RTC: ");
        Serial.println(rtcAvailable ? "READY" : "NOT AVAILABLE");
        Serial.print("SD Card: ");
        Serial.println(sdCardReady ? "READY" : "NOT AVAILABLE");
        Serial.print("CAN Bus: ");
        Serial.println(canInitialized ? "READY" : "NOT INITIALIZED");
        Serial.print("GPS: ");
        if (gpsInitialized) {
            Serial.println(gpsHasFix ? "FIX OK" : "NO FIX");
        } else {
            Serial.println("NOT READY");
        }
        Serial.print("Messages Logged: ");
        Serial.println(messageCount);
        if (sdCardReady && currentLogFileName.length() > 0) {
            Serial.print("Current Log File: ");
            Serial.println(currentLogFileName);
            Serial.print("File Size: ");
            Serial.print(currentFileSize);
            Serial.println(" bytes");
        }
        Serial.println("===================");
        lastStatusUpdate = currentTime;
    }

    // Periodic IMU output to serial (every 1 second)
    static unsigned long lastImuPrintMs = 0;
    if (millis() - lastImuPrintMs >= 1000) {
        if (imuAvailable && imuHasData) {
            Serial.printf("IMU (m/s^2) -> X: %.3f Y: %.3f Z: %.3f | G: %.3f\n",
                          linearAccelX, linearAccelY, linearAccelZ, gravityAccel);
        } else {
            Serial.println("IMU: no data yet");
        }
        lastImuPrintMs = millis();
    }
    
    // Try to reinitialize CAN if it failed (fixed 500 kbps, no auto-baud)
    static unsigned long lastCANReinitAttempt = 0;
    if (millis() - lastCANReinitAttempt >= 5000) {  // Retry every 5 seconds
        if (!canInitialized) {
            Serial.println("Retrying CAN initialization...");
            if (initCAN()) {
                Serial.println("CAN reinitialized successfully!");
                updateSystemLED();  // Update LED after CAN reinit
            } else {
                updateSystemLED();  // Update LED to show CAN failure
            }
            lastCANReinitAttempt = millis();
        }
    }
    
    // Check for SD card insertion/removal periodically
    static unsigned long lastSDCardCheck = 0;
    if (millis() - lastSDCardCheck > 2000) {
        bool previousSDState = sdCardReady;

        if (!sdCardReady) {
            // Try to detect and initialize SD card
            if (autoDetectAndInitSDCard()) {
                Serial.println("SD Card detected and initialized!");
                Serial.println("Log file will be created when data flows (CAN + IMU).");
                updateSystemLED();  // Update LED after SD card init
            }
        } else {
            // Avoid destructive access tests; rely on card type and write failures.
            uint8_t cardType = SD.cardType();
            if (cardType == CARD_NONE) {
                Serial.println("WARNING: SD Card removed!");
                closeCurrentFile();
                sdCardReady = false;
                updateSystemLED();
            }
        }

        // If SD card state changed, update LED
        if (previousSDState != sdCardReady) {
            updateSystemLED();
        }

        lastSDCardCheck = millis();
    }

    // Periodic NTP resync when idle (avoid blocking logging)
    if (wifiConnected && !dataTransferActive) {
        if ((millis() - lastNtpSyncMs) >= NTP_SYNC_INTERVAL_MS) {
            Serial.println("[NTP] Periodic sync attempt...");
            if (syncTimeFromNTP()) {
                ntpSynced = true;
                Serial.println("[NTP] Periodic sync: OK");
            } else {
                Serial.println("[NTP] Periodic sync: FAILED");
            }
            lastNtpSyncMs = millis();
        }
    }
     
     // Optional: Reset counter and print stats every 1000 messages
     if(messageCount % 1000 == 0 && messageCount > 0) {
         Serial.printf("--- %lu messages received ---\n", messageCount);
         if (sdCardReady) {
             Serial.printf("--- Current log file: %s (%lu bytes) ---\n", 
                          currentLogFileName.c_str(), currentFileSize);
         }
     }
     
     // Handle WiFi web server requests
     #if ENABLE_WIFI
     server.handleClient();
     #endif
     
    // Minimal delay to prevent overwhelming the serial port
    delayMicroseconds(100);

    // GPS LED pulse indication (non-blocking)
    updateGPSLedPulse();
}
 
 /*
  * ==================== LED COLOR SUMMARY ====================
  * 
  * LED Color Indications (as per actual code functioning):
  * 
  * RED (255, 0, 0)        - Error states (CAN init failed, SD card error, WiFi error)
  * BLUE (0, 0, 255)       - System initializing (General initialization, boot sequence, WiFi initializing)
  * YELLOW (255, 255, 0)    - CAN Bus initializing (during CAN setup process)
  * CYAN (0, 255, 255)      - SD Card setup properly (SD card initialized and ready)
  *                            Also used when AP is active and visible (AP ready for PC connection)
  * MAGENTA (255, 0, 255)   - Data transfer start (actively logging CAN messages to SD card)
  * ORANGE (255, 165, 0)    - Data transfer stopped (no data being received/logged, waiting for CAN messages)
  * GREEN (0, 255, 0)       - Connection successfully setup (WiFi STA + AP both connected and ready)
  * WHITE (255, 255, 255)   - System ready (all systems initialized, but no WiFi connection)
  * OFF (0, 0, 0)           - LED off (not used in normal operation)
  * 
  * LED Sequence During Initialization:
  * 1. BLUE - System starting, LED initializing
  * 2. YELLOW - CAN Bus initializing
  * 3. CYAN - SD Card setup properly
  * 4. CYAN/GREEN - WiFi AP ready (CYAN) or STA+AP connected (GREEN)
  * 5. ORANGE - Waiting for CAN messages (no data transfer)
  * 6. MAGENTA - Data transfer active (CAN messages being logged)
  * 
  * ============================================================
  */
 
