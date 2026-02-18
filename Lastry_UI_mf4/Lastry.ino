#include <ESP32-TWAI-CAN.hpp>
#include <sys/time.h>
#include <time.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>              // I2C library
#include <RTClib.h>            // RTC library (install via Library Manager: RTClib by Adafruit)

// ESP32-C6 I2C bus - use bus 0 for RTC
TwoWire WireRTC = TwoWire(0);  // I2C bus 0 for RTC (pins 4/5)
#include <WiFi.h>              // WiFi library for ESP32-C6
#include <WebServer.h>         // Web server for file access
#include <cstring>

// RGB LED Status Indicator Configuration
// LED Colors:
//   GREEN  = CAN messages receiving AND logging to SD card (all good)
//   YELLOW = CAN messages receiving but NOT logging to SD card
//   WHITE  = SD card present but NO CAN messages being received
//   RED    = CAN bus module not working/failed OR no CAN messages AND SD card removed
//   BLUE   = Initializing (during setup)

// Option 1: NeoPixel/WS2812B RGB LED (recommended for built-in LED)
// Uncomment the following lines and install "Adafruit NeoPixel" library
#define USE_NEOPIXEL
#define RGB_LED_PIN    8       // Pin for NeoPixel data (adjust based on your board)

// Option 2: Separate RGB pins (for common cathode RGB LED)
// Default: Using separate RGB pins with PWM
#define RGB_RED_PIN     8       // Red LED pin (PWM capable)
#define RGB_GREEN_PIN   9       // Green LED pin (PWM capable)
#define RGB_BLUE_PIN    10      // Blue LED pin (PWM capable)

#ifdef USE_NEOPIXEL
// Try to include NeoPixel library - will fail gracefully if not installed
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel rgbLED(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
#else
// PWM channels for RGB LED
#define RGB_RED_CHANNEL    0
#define RGB_GREEN_CHANNEL  1
#define RGB_BLUE_CHANNEL   2
#define RGB_PWM_FREQ       5000
#define RGB_PWM_RESOLUTION 8    // 8-bit resolution (0-255)
#endif

// General Purpose CAN Bus Monitor with RTC Support and SD Card Logging
// Supports DS1307, DS3231, PCF8523, PCF8563 RTC modules
// Falls back to compile-time if RTC not available
// For ESP32-C6-Pico development board

// Pin definitions for ESP32-C6
#define CAN_TX		7
#define CAN_RX		6

// SD Card pins for ESP32-C6
#define SD_CS		20
#define SD_MOSI		19
#define SD_MISO		18
#define SD_SCK		21

// RTC I2C bus (separate, used only during initialization)
#define RTC_SDA_PIN    4       // I2C SDA pin for RTC (ESP32-C6)
#define RTC_SCL_PIN    5       // I2C SCL pin for RTC (ESP32-C6)

// SD Card settings
#define SD_CARD_SPEED	4000000  // 4MHz SPI speed
#define MAX_FILE_SIZE	10485760 // 10MB max file size
#define LOG_FILE_PREFIX	"CAN_LOG_"

// Custom filename prefix (can be set via serial command)
String customFilePrefix = "";

// WiFi Configuration
#define ENABLE_WIFI     1       // Set to 1 to enable WiFi, 0 to disable

// WiFi Access Point Configuration
#define WIFI_AP_SSID    "CAN_Logger"      // WiFi network name
#define WIFI_AP_PASS   "CANLogger123"     // WiFi password (min 8 characters)
#define WIFI_AP_IP     IPAddress(192, 168, 4, 1)  // Access Point IP
#define WIFI_AP_GATEWAY IPAddress(192, 168, 4, 1)
#define WIFI_AP_SUBNET IPAddress(255, 255, 255, 0)

// WiFi Station Configuration (placeholders - fill in your network)
#define WIFI_STA_SSID   "AkashGanga"
#define WIFI_STA_PASS   "Naxatra2025"

// NTP configuration (used to correct RTC when WiFi STA is connected)
#define NTP_SERVER        "pool.ntp.org"
#define GMT_OFFSET_SEC    19800      // timezone offset in seconds (example: IST = +5:30 = 19800)
#define DST_OFFSET_SEC    0          // daylight savings offset in seconds
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

// Using CSV format for Python DBC decoding

// RTC object - Change to RTC_DS1307, RTC_PCF8523, or RTC_PCF8563 if needed
RTC_DS3231 rtc;  // DS3231 is recommended (very accurate)

bool rtcAvailable = false;


CanFrame rxFrame;
unsigned long messageCount = 0;
unsigned long bootMillis;
time_t baseTime;

// SD Card variables
File logFile;
String currentLogFileName;
unsigned long currentFileSize = 0;
bool sdCardReady = false;

// System status tracking
bool canBusReady = false;
bool loggingEnabled = false;  // User-controlled logging flag
bool stoppedByUser = false;   // Set when user issues STOP_LOG
unsigned long lastCanMessageTime = 0;
unsigned long lastStatusUpdate = 0;
unsigned long lastSDCardCheck = 0;
#define SD_CARD_CHECK_INTERVAL 2000  // Check for SD card every 2 seconds

// WiFi objects
#if ENABLE_WIFI
WebServer server(80);  // Web server on port 80
#endif


bool syncTimeWithNTP() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected - cannot sync NTP");
        return false;
    }
    
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    delay(2000);
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        time_t ntpTime = mktime(&timeinfo);
        baseTime = ntpTime;
        bootMillis = millis();
        
        // Update RTC if available
        if (rtcAvailable) {
            setRTCTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
        return true;
    }
    return false;
}
bool writeFileHeader();
bool writeCANMessage(const CanFrame& frame, time_t timestamp, uint32_t micros);
String generateLogFileName();
void closeCurrentFile();
void initializeRGBLED();
void setLEDColor(uint8_t red, uint8_t green, uint8_t blue);
void updateLEDStatus();
bool initializeWiFi();
bool syncTimeWithNTP();
void handleRoot();
void handleFileList();
void handleFileDownload();
bool writePlainLine(const String& line);
void storeLiveFrame(const CanFrame& frame, time_t timestamp, uint32_t micros, const String& formattedTime);
void handleLiveData();

// Initialize RGB LED
void initializeRGBLED() {
#ifdef USE_NEOPIXEL
    // Initialize NeoPixel
    rgbLED.begin();
    rgbLED.setBrightness(50);  // Set brightness (0-255)
    rgbLED.show();
    if (Serial) Serial.println("RGB LED (NeoPixel) initialized");
#else
    // Setup PWM for RGB pins
    ledcSetup(RGB_RED_CHANNEL, RGB_PWM_FREQ, RGB_PWM_RESOLUTION);
    ledcSetup(RGB_GREEN_CHANNEL, RGB_PWM_FREQ, RGB_PWM_RESOLUTION);
    ledcSetup(RGB_BLUE_CHANNEL, RGB_PWM_FREQ, RGB_PWM_RESOLUTION);
    
    ledcAttachPin(RGB_RED_PIN, RGB_RED_CHANNEL);
    ledcAttachPin(RGB_GREEN_PIN, RGB_GREEN_CHANNEL);
    ledcAttachPin(RGB_BLUE_PIN, RGB_BLUE_CHANNEL);
    
    // Turn off LED initially
    setLEDColor(0, 0, 0);
    if (Serial) Serial.println("RGB LED (PWM) initialized");
#endif
}

// Set RGB LED color
// Note: For NeoPixel with NEO_GRB, we need to pass colors in GRB order to Color()
void setLEDColor(uint8_t red, uint8_t green, uint8_t blue) {
#ifdef USE_NEOPIXEL
    // NEO_GRB means the LED expects Green, Red, Blue order
    // So we pass (green, red, blue) to Color() to get correct display
    rgbLED.setPixelColor(0, rgbLED.Color(green, red, blue));
    rgbLED.show();
#else
    ledcWrite(RGB_RED_CHANNEL, red);
    ledcWrite(RGB_GREEN_CHANNEL, green);
    ledcWrite(RGB_BLUE_CHANNEL, blue);
#endif
}

// Update LED status based on system state
void updateLEDStatus() {
    // Check system status
    bool canWorking = canBusReady;
    bool sdWorking = sdCardReady;
    bool rtcWorking = rtcAvailable;
    bool receivingMessages = (millis() - lastCanMessageTime) < 5000; // Messages in last 5 seconds
    
    // Debug output (can be removed later)
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 5000) {  // Every 5 seconds
        if (Serial) {
            Serial.printf("[LED Status] CAN:%d SD:%d RTC:%d Msgs:%d\n", 
                         canWorking, sdWorking, rtcWorking, receivingMessages);
        }
        lastDebug = millis();
    }
    
    // Priority-based status determination (most critical first)
    
    // 1. CAN bus failure (most critical - Red)
    if (!canWorking) {
        setLEDColor(255, 0, 0);  // Red - CAN bus module not working
        return;
    }
    
    // Priority for user-controlled states:
    // - When logging is active -> GREEN
    // - When user stopped logging -> ORANGE
    // - Until the user starts logging after initialization -> WHITE (if SD present)
    if (loggingEnabled) {
        setLEDColor(0, 255, 0);  // Green - actively logging
        return;
    }

    if (stoppedByUser) {
        setLEDColor(255, 165, 0); // Orange - stopped by user
        return;
    }

    // If not logging and not stopped by user, show white when SD present (idle)
    if (sdWorking) {
        setLEDColor(255, 255, 255);  // White - idle but SD present
    } else {
        // If SD missing while not logging, show red to indicate problem
        setLEDColor(255, 0, 0);
    }
    return;
}

// Initialize RTC module on separate I2C bus (pins 4/5)
bool initializeRTC() {
    Serial.println("Initializing RTC on I2C bus 0 (pins 4/5)...");
    
    // Initialize I2C bus 0 on RTC pins (4/5)
    WireRTC.begin(RTC_SDA_PIN, RTC_SCL_PIN);
    delay(100);
    
    // Try to initialize RTC with WireRTC
    if (!rtc.begin(&WireRTC)) {
        Serial.println("WARNING: RTC not found!");
        Serial.println("Check RTC wiring (SDA=pin 4, SCL=pin 5)");
        Serial.println("Falling back to compile-time");
        return false;
    }
    
    // Check if RTC lost power (battery dead or first time)
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
        Serial.println("Use SET_TIME command to set correct time.");
    } else {
        Serial.println("RTC found and time is valid!");
    }
    
    // Display current RTC time
    DateTime now = rtc.now();
    Serial.printf("RTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
    
    rtcAvailable = true;
    return true;
}

// Disable RTC I2C bus (call after RTC operations complete)
void disableRTCI2C() {
    Serial.println("Disabling RTC I2C bus 0 (pins 4/5)...");
    WireRTC.end();
    delay(50);  // Small delay to ensure bus is released
    Serial.println("RTC I2C bus disabled");
}

// Get current time from RTC
time_t getRTCTime() {
    if (!rtcAvailable) return 0;
    
    // Switch bus 0 to RTC pins temporarily
    WireRTC.end();
    delay(10);
    WireRTC.begin(RTC_SDA_PIN, RTC_SCL_PIN);
    delay(50);
    
    DateTime now = rtc.now();
    struct tm timeStruct = {0};
    timeStruct.tm_year = now.year() - 1900;
    timeStruct.tm_mon = now.month() - 1;
    timeStruct.tm_mday = now.day();
    timeStruct.tm_hour = now.hour();
    timeStruct.tm_min = now.minute();
    timeStruct.tm_sec = now.second();
    
    // Release RTC I2C bus
    WireRTC.end();
    delay(10);
    
    return mktime(&timeStruct);
}

// Set RTC time
void setRTCTime(int year, int month, int day, int hour, int minute, int second) {
    if (rtcAvailable) {
        // Switch bus 0 to RTC pins temporarily
        WireRTC.end();
        delay(10);
        WireRTC.begin(RTC_SDA_PIN, RTC_SCL_PIN);
        delay(50);
        
        rtc.adjust(DateTime(year, month, day, hour, minute, second));
        Serial.printf("RTC time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                     year, month, day, hour, minute, second);
        
        // Release RTC I2C bus
        WireRTC.end();
        delay(10);
    }
}

// Check if SD card is physically present (quick check without full initialization)
bool checkSDCardPresent() {
    // Quick check: try to detect card type
    // This is a lightweight check that doesn't require full initialization
    uint8_t cardType = SD.cardType();
    return (cardType != CARD_NONE);
}

// Test if SD card is actually accessible by attempting a write operation
// This is more reliable than just checking cardType() for removal detection
bool testSDCardAccess() {
    // Try to open root directory (tests file system access)
    File root = SD.open("/", FILE_READ);
    if (!root) {
        return false;  // Can't access root - card likely removed
    }
    root.close();
    
    // Try to create and write a test file
    File testFile = SD.open("/_test.tmp", FILE_WRITE);
    if (!testFile) {
        return false;  // Can't write - card likely removed or write-protected
    }
    
    // Try to write a byte
    if (testFile.write('T') != 1) {
        testFile.close();
        SD.remove("/_test.tmp");
        return false;  // Write failed
    }
    
    // Flush to ensure data is written
    testFile.flush();
    testFile.close();
    
    // Try to delete the test file
    SD.remove("/_test.tmp");
    
    return true;  // All tests passed - card is accessible and writable
}

// Test if CAN transceiver is still accessible
// Attempts to reinitialize CAN bus - if it fails, transceiver is likely removed
// SD Card initialization
bool initializeSDCard() {
    Serial.println("Initializing SD Card...");
    
    // Note: SPI should already be initialized in setup(), but ensure it's ready
    // SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);  // Already done in setup()
    
    // Initialize SD card
    if (!SD.begin(SD_CS, SPI, SD_CARD_SPEED)) {
        Serial.println("ERROR: SD Card initialization failed!");
        Serial.println("Check SD card connection and format (FAT32)");
        return false;
    }
    
    // Check card type and size
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("ERROR: No SD card found!");
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
    
    Serial.println("SD Card initialized successfully!");
    return true;
}

// Automatically detect and initialize SD card if inserted after startup
bool autoDetectAndInitSDCard() {
    // Only check if SD card is not already ready
    if (sdCardReady) {
        return true;  // Already initialized
    }
    
    // Try to initialize SD card (this will detect if card is present)
    // SD.begin() will return false if no card, true if card is present and initialized
    if (SD.begin(SD_CS, SPI, SD_CARD_SPEED)) {
        // Check if card is actually present
        uint8_t cardType = SD.cardType();
        if (cardType != CARD_NONE) {
            // Card detected! Complete initialization
            Serial.println("SD Card detected! Initializing...");
            
            // Get card info
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
            
            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.printf("SD Card Size: %lluMB\n", cardSize);
            
            // Update baseTime to current time when SD card is inserted
            // This ensures log file has correct timestamp
            if (rtcAvailable) {
                baseTime = getRTCTime();
                bootMillis = millis();  // Reset boot reference
                Serial.println("Using RTC time for log file");
            } else if (baseTime == 0) {
                // If no RTC and no baseTime, use compile time
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
                Serial.println("Using compile time for log file");
            }
            
            // Do NOT auto-create log file on card insertion.
            // The system will detect SD card presence but will only create files
            // when the user explicitly requests via CREATE_FILE command.
            Serial.println("SD Card detected (no file created). Waiting for CREATE_FILE command.");
            sdCardReady = true;
            updateLEDStatus();  // Update LED to show new status
            return true;
        }
    }
    
    return false;  // No card detected or initialization failed
}

bool writePlainLine(const String& line) {
    if (!logFile) return false;
    size_t written = logFile.println(line);
    if (written == 0) {
        Serial.println("ERROR: Failed to write log line");
        return false;
    }
    currentFileSize += written;
    return true;
}

// Generate log file name with timestamp
String generateLogFileName() {
    String prefix = customFilePrefix.length() > 0 ? customFilePrefix : LOG_FILE_PREFIX;
    struct tm* timeinfo = localtime(&baseTime);
    char fileName[64]; // Increased size for longer prefix
    sprintf(fileName, "/%s%04d%02d%02d_%02d%02d%02d.csv",
            prefix.c_str(),
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec);
    return String(fileName);
}

// Create new log file
bool createNewLogFile() {
    // Close existing file if open
    if (logFile) {
        logFile.close();
    }
    
    // Generate new filename
    currentLogFileName = generateLogFileName();
    
    Serial.printf("Creating log file: %s\n", currentLogFileName.c_str());
    
    // Create new file
    logFile = SD.open(currentLogFileName, FILE_WRITE);
    if (!logFile) {
        Serial.printf("ERROR: Failed to create log file: %s\n", currentLogFileName.c_str());
        return false;
    }
    
    Serial.printf("Successfully created log file: %s\n", currentLogFileName.c_str());
    
    // Write file header
    if (!writeFileHeader()) {
        Serial.println("ERROR: Failed to write file header!");
        logFile.close();
        return false;
    }
    
    currentFileSize = logFile.size();
    Serial.printf("File header written successfully. Initial size: %lu bytes\n", currentFileSize);
    return true;
}

// Write CSV file header
bool writeFileHeader() {
    if (!logFile) return false;
 
    String headerLine = "Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC";
    for (int i = 0; i < 8; i++) {
        headerLine += ",Data" + String(i);
    }
    
    if (!writePlainLine(headerLine)) {
        Serial.println("ERROR: Failed to write CSV header");
        return false;
    }
    logFile.flush();
    return true;
}

// Write CAN message to log file (CSV line)
bool writeCANMessage(const CanFrame& frame, time_t timestamp, uint32_t micros) {
    if (!logFile || !sdCardReady) return false;
    
    // Check if we need to stop logging due to file size limit
    if (currentFileSize > MAX_FILE_SIZE) {
        Serial.println("ERROR: File size limit reached! Logging stopped.");
        Serial.println("Create a new file to continue logging.");
        loggingEnabled = false;
        stoppedByUser = true;
        closeCurrentFile();
        updateLEDStatus();
        return false;
    }
    
    // Format timestamp string
    struct tm* timeinfo = localtime(&timestamp);
    char timeStr[32];
    sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    String line;
    line.reserve(96);
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
    
    for (int i = 0; i < 8; i++) {
        line += ",";
        if (i < frame.data_length_code) {
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
    
    if (!writePlainLine(line)) {
        return false;
    }
    
    logFile.flush(); // Ensure data is written to card
    
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

void handleLiveData() {
#if ENABLE_WIFI
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
                continue; // overwritten entry
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
            } else if (frame.unixTime > 0) {
                // Fallback to unix time formatting to avoid blank timestamps
                time_t t = (time_t)frame.unixTime;
                struct tm* ti = localtime(&t);
                char ts[24];
                snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
                         ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
                         ti->tm_hour, ti->tm_min, ti->tm_sec);
                json += ts;
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

            // Always emit 8 data bytes (zero-padded) so the GUI table stays aligned
            json += ",\"data\":[";
            for (int i = 0; i < 8; i++) {
                if (i > 0) {
                    json += ",";
                }
                char byteStr[6];
                snprintf(byteStr, sizeof(byteStr), "\"%02X\"", frame.data[i]);
                json += byteStr;
            }
            json += "]}";
        }
    }
    
    json += "]}";
    server.send(200, "application/json", json);
#endif
}

// Close current log file
void closeCurrentFile() {
    if (logFile) {
        logFile.close();
        Serial.printf("Closed log file: %s (Size: %lu bytes)\n", 
                     currentLogFileName.c_str(), currentFileSize);
    }
}

// Initialize WiFi Access Point + optional Station
#if ENABLE_WIFI
bool initializeWiFi() {
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
    Serial.println("Connect to this network and open http://192.168.4.1 in your browser");

    // Try to connect as a Station (optional; uses placeholders)
    if (strlen(WIFI_STA_SSID) > 0 && strcmp(WIFI_STA_SSID, "YOUR_STA_SSID") != 0) {
        Serial.print("Connecting to STA network: ");
        Serial.println(WIFI_STA_SSID);
        WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);

        unsigned long startAttempt = millis();
        const unsigned long connectTimeout = 10000; // 10 seconds
        while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < connectTimeout) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("STA connected! IP: ");
            Serial.println(WiFi.localIP());
            
            // Configure NTP
            configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
            Serial.println("NTP configured. Syncing time...");
            
            // Wait a bit for NTP sync
            delay(2000);
            
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                Serial.println("NTP sync successful!");
                time_t ntpTime = mktime(&timeinfo);
                baseTime = ntpTime;
                bootMillis = millis();
                
                // Update RTC if available
                if (rtcAvailable) {
                    setRTCTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                    Serial.println("RTC updated with NTP time");
                }
            } else {
                Serial.println("NTP sync failed");
            }
        } else {
            Serial.println("WARNING: STA connection failed (continuing AP-only).");
        }
    } else {
        Serial.println("STA credentials not set (placeholders) - AP-only mode.");
    }
    
    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/files", handleFileList);
    server.on("/download", handleFileDownload);
    server.on("/live", handleLiveData);
    server.onNotFound([]() {
        server.send(404, "text/plain", "File not found");
    });
    
    // Start web server
    server.begin();
    Serial.println("Web server started on http://192.168.4.1");
    
    return true;
}

// Web server root handler - file browser
void handleRoot() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>CAN Logger - File Browser</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f5f5f5;}";
    html += "h1{color:#333;}table{width:100%;border-collapse:collapse;background:white;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    html += "th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd;}";
    html += "th{background-color:#4CAF50;color:white;}tr:hover{background-color:#f5f5f5;}";
    html += "a{color:#4CAF50;text-decoration:none;}a:hover{text-decoration:underline;}";
    html += ".info{background:#e7f3ff;padding:15px;border-radius:5px;margin-bottom:20px;}";
    html += "</style></head><body>";
    html += "<h1>📁 CAN Logger - File Browser</h1>";
    html += "<div class='info'><strong>Status:</strong> ";
    html += "CAN: " + String(canBusReady ? "Ready" : "Not Ready") + " | ";
    html += "SD: " + String(sdCardReady ? "Ready" : "Not Ready") + " | ";
    html += "Messages: " + String(messageCount) + "</div>";
    html += "<h2>Log Files on SD Card</h2>";
    html += "<table><tr><th>File Name</th><th>Size</th><th>Action</th></tr>";
    
    // List files from SD card
    if (sdCardReady) {
        File root = SD.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
                    String fileName = String(file.name());
                    // Remove leading slash if present
                    if (fileName.startsWith("/")) {
                        fileName = fileName.substring(1);
                    }
                    size_t fileSize = file.size();
                    html += "<tr><td>" + fileName + "</td>";
                    html += "<td>" + String(fileSize) + " bytes</td>";
                    html += "<td><a href='/download?file=" + fileName + "'>Download</a></td></tr>";
                }
                file = root.openNextFile();
            }
            root.close();
        }
    } else {
        html += "<tr><td colspan='3'>SD Card not available</td></tr>";
    }
    
    html += "</table>";
    html += "<p><a href='/files'>Refresh</a></p>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

// File list handler (JSON)
void handleFileList() {
    String json = "[";
    bool first = true;
    
    if (sdCardReady) {
        File root = SD.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
                    if (!first) json += ",";
                    first = false;
                    String fileName = String(file.name());
                    if (fileName.startsWith("/")) {
                        fileName = fileName.substring(1);
                    }
                    json += "{\"name\":\"" + fileName + "\",\"size\":" + String(file.size()) + "}";
                }
                file = root.openNextFile();
            }
            root.close();
        }
    }
    
    json += "]";
    server.send(200, "application/json", json);
}

// File download handler
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
    
    // Security: prevent directory traversal
    if (fileName.indexOf("..") >= 0 || fileName.indexOf("/") >= 0) {
        server.send(403, "text/plain", "Invalid file name");
        return;
    }
    
    File file = SD.open("/" + fileName);
    if (!file) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    // Send file with proper headers
    server.streamFile(file, "text/csv");
    file.close();
}
#endif


void setup() {
    // Setup serial communication FIRST
    // ESP32-C6 uses USB Serial, so Serial should always be available
    Serial.begin(115200);
    
    // For ESP32-C6, Serial is usually always available via USB
    // But wait briefly to ensure it's ready
    delay(1000);
    
    // Clear any pending serial data
    while(Serial.available()) {
        Serial.read();
    }
    
    // Immediate test - this should appear if Serial is working
    // Use multiple methods to ensure output
    Serial.print("\n\n\n");
    Serial.print("========================================\n");
    Serial.print("=== CAN Bus Monitor with RTC Support ===\n");
    Serial.print("========================================\n");
    Serial.print("Serial communication test: OK\n");
    Serial.print("Baud rate: 115200\n");
    Serial.print("Starting initialization...\n");
    delay(100);  // Give time for data to be sent
    
    // Initialize RGB LED (may fail if NeoPixel library not installed)
    initializeRGBLED();
    setLEDColor(0, 0, 255);  // Blue during initialization
    
    // Record boot time in milliseconds
    bootMillis = millis();
    
    // ===== STEP 1: Initialize RTC on separate I2C bus (pins 4/5) =====
    Serial.println("Step 1: Initializing RTC...");
    rtcAvailable = initializeRTC();
    
    // Read time from RTC if available
    if (rtcAvailable) {
        baseTime = getRTCTime();
        bootMillis = millis();  // Reset boot reference
        Serial.println("RTC time read successfully");
    }
    
    // ===== STEP 2: Initialize WiFi and sync RTC with NTP if needed =====
    #if ENABLE_WIFI
    Serial.println("Step 2: Initializing WiFi...");
    if (initializeWiFi()) {
        Serial.println("WiFi initialized");
    }
    #endif
    
    // ===== STEP 3: Establish base time =====
    
    if (rtcAvailable) {
        // Use RTC time (already set above)
        Serial.println("Using RTC for time synchronization");
    } else {
        // Fallback: Use compile-time date/time
        Serial.println("Using compile-time as fallback...");
        
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
        Serial.printf("Compile time: %04d-%02d-%02d %02d:%02d:%02d\n",
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
    
    // Allow time setting via Serial (updates both RTC and baseTime)
    Serial.println("You can set time via Serial Monitor:");
    Serial.println("Send: SET_TIME,YYYY,MM,DD,HH,MM,SS");
    Serial.println("Example: SET_TIME,2025,06,20,14,30,45");
    Serial.println("Waiting 10 seconds for time input...");
    
    unsigned long startWait = millis();
    while (millis() - startWait < 2000) {
        if (Serial.available()) {
            String input = "";
            unsigned long readStart = millis();
            while (Serial.available() && (millis() - readStart < 200)) {
                char c = Serial.read();
                if (c == '\n' || c == '\r') {
                    break;
                }
                input += c;
            }
            input.trim();
            if (input.length() > 0) {
                Serial.printf("Received: %s\n", input.c_str());
                if (parseTimeInput(input)) {
                    Serial.println("Time updated from Serial input!");
                    break;
                }
            }
        }
        delay(100);
    }
    Serial.println("Continuing with initialization...");
    
    // Configure CAN pins
    ESP32Can.setPins(CAN_TX, CAN_RX);
    
    // Set queue sizes for monitoring
    ESP32Can.setRxQueueSize(100);
    ESP32Can.setTxQueueSize(10);
    
    // Set CAN bus speed
    ESP32Can.setSpeed(ESP32Can.convertSpeed(500));
    
    // Initialize CAN bus
    Serial.println("Initializing CAN bus...");
    
    if(ESP32Can.begin()) {
        Serial.println("CAN Bus initialized successfully!");
        Serial.println("Speed: 500 kbps");
        Serial.println("Format: [YYYY-MM-DD HH:MM:SS][COUNT] ID:DATA");
        Serial.println("----------------------------------------");
        canBusReady = true;
    } else {
        Serial.println("ERROR: CAN Bus initialization failed!");
        Serial.println("Check wiring and connections.");
        Serial.println("System will continue but CAN will not work.");
        canBusReady = false;
        setLEDColor(255, 0, 0);  // Red - CAN bus failed
        // Don't block - allow system to continue
        delay(2000);
    }
    
    // Initialize SPI for SD card (needed for detection even if card not present)
    Serial.println("Initializing SPI for SD card...");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    // Try to initialize SD Card
    sdCardReady = initializeSDCard();
    if (sdCardReady) {
        Serial.println("SD Card detected and ready!");
        Serial.println("Logging is currently disabled. Send 'START_LOG' to begin logging.");
    } else {
        Serial.println("WARNING: SD Card not available at startup");
        Serial.println("System will auto-detect SD card if inserted later");
        Serial.println("CAN messages will be displayed but not logged until SD card is inserted and logging is started");
    }
    
    Serial.println("System ready! Waiting for user commands to start logging.");
    Serial.println("Available commands: START_LOG, STOP_LOG, SET_FILENAME,<name>, SYNC_TIME, STATUS");
    Serial.println("NOTE: CAN messages will be displayed but NOT logged until START_LOG is sent.");
    
    // Update LED status after initialization (shows final status)
    updateLEDStatus();
}

bool parseTimeInput(String input) {
    // Convert to uppercase for case-insensitive matching
    input.toUpperCase();
    input.trim();
    
    if (input.startsWith("SET_TIME,")) {
        // Parse: SET_TIME,YYYY,MM,DD,HH,MM,SS
        input.remove(0, 9); // Remove "SET_TIME,"
        input.trim();
        
        int values[6];
        int index = 0;
        int lastComma = -1;
        
        // Parse comma-separated values
        for (int i = 0; i <= input.length() && index < 6; i++) {
            if (i == input.length() || input.charAt(i) == ',') {
                if (i > lastComma + 1) {
                    String valueStr = input.substring(lastComma + 1, i);
                    valueStr.trim();
                    values[index] = valueStr.toInt();
                    index++;
                }
                lastComma = i;
            }
        }
        
        // Validate we got all 6 values and they're reasonable
        if (index == 6) {
            // Basic validation
            if (values[0] >= 2020 && values[0] <= 2100 &&  // Year
                values[1] >= 1 && values[1] <= 12 &&         // Month
                values[2] >= 1 && values[2] <= 31 &&         // Day
                values[3] >= 0 && values[3] <= 23 &&         // Hour
                values[4] >= 0 && values[4] <= 59 &&         // Minute
                values[5] >= 0 && values[5] <= 59) {        // Second
                
                struct tm newTime = {0};
                newTime.tm_year = values[0] - 1900;  // Year since 1900
                newTime.tm_mon = values[1] - 1;      // Month (0-11)
                newTime.tm_mday = values[2];         // Day
                newTime.tm_hour = values[3];         // Hour
                newTime.tm_min = values[4];          // Minute
                newTime.tm_sec = values[5];          // Second
                
                time_t newTimeT = mktime(&newTime);
                if (newTimeT != -1) {
                    baseTime = newTimeT;
                    bootMillis = millis(); // Reset boot reference
                    
                    // Also update RTC if available
                    // Note: Need to temporarily enable RTC I2C bus
                    if (rtcAvailable) {
                        setRTCTime(values[0], values[1], values[2], 
                                   values[3], values[4], values[5]);
                        Serial.println("RTC updated with new time");
                    }
                    
                    Serial.printf("Time updated to: %04d-%02d-%02d %02d:%02d:%02d\n",
                                 values[0], values[1], values[2], 
                                 values[3], values[4], values[5]);
                    return true;
                } else {
                    Serial.println("ERROR: Invalid time value (mktime failed)");
                }
            } else {
                Serial.println("ERROR: Invalid time values (out of range)");
                Serial.println("Expected: SET_TIME,YYYY,MM,DD,HH,MM,SS");
                Serial.println("Example: SET_TIME,2025,06,20,14,30,45");
            }
        } else {
            Serial.println("ERROR: Invalid time format!");
            Serial.println("Expected: SET_TIME,YYYY,MM,DD,HH,MM,SS");
            Serial.println("Example: SET_TIME,2025,06,20,14,30,45");
        }
    }
    return false;
}

String getFormattedTime() {
    time_t currentTime = 0;
    
    if (rtcAvailable) {
        // Use RTC for accurate time (but don't access RTC in loop - use baseTime + elapsed)
        // RTC I2C bus is disabled after initialization, so we calculate from baseTime
        unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
        currentTime = baseTime + elapsedSeconds;
    } else if (baseTime != 0) {
        // Fallback: Calculate current time based on elapsed milliseconds since boot
        unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
        currentTime = baseTime + elapsedSeconds;
    } else {
        // Fallback to millis if no time set
        return String(millis());
    }
    
    struct tm* timeinfo = localtime(&currentTime);
    
    char timeStr[32];
    sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    return String(timeStr);
}

void loop() {
    // Check for time update commands and other serial input
    // Process serial input immediately when available
    if (Serial.available() > 0) {
        // Read input with timeout to avoid blocking
        String input = "";
        unsigned long startRead = millis();
        // Read until newline or timeout (200ms)
        while ((millis() - startRead < 200)) {
            if (Serial.available()) {
                char c = Serial.read();
                if (c == '\n' || c == '\r') {
                    break;  // End of line
                }
                if (c >= 32 && c <= 126) {  // Only printable ASCII
                    input += c;
                }
            } else {
                delay(1);  // Small delay if no data available
            }
        }
        input.trim();
        
        // Only process non-empty input
        if (input.length() > 0) {
            // Echo received command for debugging (cross-platform compatible)
            Serial.print("> ");
            Serial.println(input);
            Serial.flush();  // Ensure echo is sent immediately
            
            // Log command reception for debugging
            Serial.printf("[CMD] Received: '%s' (len=%d)\n", input.c_str(), input.length());
            Serial.flush();

            if (parseTimeInput(input)) {
                // Time was updated, continue
                Serial.println("[CMD] Processed: TIME UPDATE");
                Serial.flush();
            }
            else if (input.startsWith("SET_FILENAME")) {
                // Accept both "SET_FILENAME,<name>" and "SET_FILENAME <name>"
                String filename = input.substring(12); // Remove "SET_FILENAME"
                filename.trim();
                if (filename.startsWith(",") || filename.startsWith(" ")) {
                    filename = filename.substring(1);
                    filename.trim();
                }
                if (filename.length() > 0 && filename.length() <= 20) {
                    customFilePrefix = filename;
                    Serial.printf("Filename set: %s\n", customFilePrefix.c_str());
                    Serial.printf("[CMD] Processed: SET_FILENAME '%s'\n", customFilePrefix.c_str());
                    Serial.flush();
                } else {
                    Serial.printf("ERROR: Invalid filename. Must be 1-20 characters. Got: %d chars\n", filename.length());
                    Serial.flush();
                }
            }
            else if (input == "START_LOG") {
                // Start logging only when explicitly requested
                Serial.println("[CMD] Processing START_LOG");
                Serial.flush();
                
                // Always (re)initialize SD before starting, in case a new PC toggled USB power
                if (!sdCardReady) {
                    // Try to re-initialize SD card
                    if (!initializeSDCard()) {
                        Serial.println("ERROR: SD Card not available!");
                        Serial.println("Ensure SD card is inserted and try again.");
                    }
                }
                if (!sdCardReady) {
                    Serial.println("ERROR: SD Card not available!");
                } else {
                    if (!logFile) {
                        Serial.println("ERROR: No log file open. Use CREATE_FILE first.");
                        Serial.printf("Current log file: %s\n", currentLogFileName.c_str());
                        Serial.println("Send CREATE_FILE to create a new log file before START_LOG.");
                        updateLEDStatus();
                        return;
                    }
                    loggingEnabled = true;
                    stoppedByUser = false;
                    Serial.println("Logging started!");
                    Serial.printf("Logging to: %s\n", currentLogFileName.c_str());
                    Serial.println("CAN messages will be recorded to SD card.");
                    Serial.println("[CMD] START_LOG: SUCCESS");
                    Serial.flush();
                }
                updateLEDStatus();
            }
            else if (input == "STOP_LOG") {
                // Stop logging immediately
                Serial.println("[CMD] Processing STOP_LOG");
                Serial.flush();
                loggingEnabled = false;
                stoppedByUser = true;
                closeCurrentFile();
                Serial.println("Logging stopped");
                Serial.println("[CMD] STOP_LOG: SUCCESS");
                Serial.flush();
                updateLEDStatus();
            }
                else if (input == "CREATE_FILE") {
                    // Create a new log file using the previously set customFilePrefix
                    Serial.println("[CMD] Processing CREATE_FILE");
                    Serial.flush();
                    if (!sdCardReady) {
                        sdCardReady = initializeSDCard();
                    }
                    if (!sdCardReady) {
                        Serial.println("ERROR: SD Card not available!");
                        Serial.println("Ensure SD card is properly inserted.");
                        Serial.println("[CMD] CREATE_FILE: FAILED - No SD card");
                        Serial.flush();
                    } else {
                        if (customFilePrefix.length() == 0) {
                            // Fall back to default prefix so any PC can create a file without prior SET_FILENAME
                            customFilePrefix = LOG_FILE_PREFIX;
                            Serial.printf("No filename prefix supplied. Using default: %s\n", LOG_FILE_PREFIX);
                        }
                        if (logFile) {
                            logFile.close();
                            Serial.println("Closed existing log file.");
                        }
                        // Create new file
                        if (createNewLogFile()) {
                            Serial.printf("FILE_CREATED:%s\n", currentLogFileName.c_str());
                            Serial.println("File ready. Use START_LOG to begin recording.");
                            Serial.printf("[CMD] CREATE_FILE: SUCCESS - %s\n", currentLogFileName.c_str());
                            Serial.flush();
                        } else {
                            Serial.printf("ERROR: Failed to create log file with prefix: %s\n", customFilePrefix.c_str());
                        }
                    }
                    updateLEDStatus();
                }
            else if (input == "SYNC_TIME") {
                Serial.println("Attempting to sync time with NTP...");
                if (syncTimeWithNTP()) {
                    Serial.println("Time synced successfully with NTP");
                } else {
                    Serial.println("Failed to sync time with NTP");
                }
                // Do not auto-create files or start logging after time sync - user must explicitly CREATE_FILE then START_LOG
                if (!sdCardReady) {
                    sdCardReady = initializeSDCard();
                }
                if (sdCardReady) {
                    Serial.println("SD Card available. Use CREATE_FILE to create a new log file.");
                } else {
                    Serial.println("ERROR: SD Card not available!");
                }
                updateLEDStatus();  // Update LED status
            }
            else if (input == "STATUS") {
                Serial.printf("RTC: %s\n", rtcAvailable ? "Available" : "Not Available");
                if (rtcAvailable) {
                    // Switch bus 0 to RTC pins temporarily
                    WireRTC.end();
                    delay(10);
                    WireRTC.begin(RTC_SDA_PIN, RTC_SCL_PIN);
                    delay(50);
                    DateTime now = rtc.now();
                    Serial.printf("RTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                                 now.year(), now.month(), now.day(),
                                 now.hour(), now.minute(), now.second());
                    WireRTC.end();
                    delay(10);
                }
                Serial.printf("CAN Bus: %s\n", canBusReady ? "Ready" : "Not Ready");
                Serial.printf("SD Card: %s\n", sdCardReady ? "Ready" : "Not Ready");
                Serial.printf("Logging Enabled: %s\n", loggingEnabled ? "Yes" : "No");
                Serial.printf("Current File: %s\n", currentLogFileName.c_str());
                Serial.printf("File Size: %lu bytes\n", currentFileSize);
                Serial.printf("Messages Logged: %lu\n", messageCount);
                bool receivingMessages = (millis() - lastCanMessageTime) < 5000;
                Serial.printf("Receiving Messages: %s\n", receivingMessages ? "Yes" : "No");
            }
            else {
                // Unknown command
                Serial.println("Unknown command. Available commands:");
                Serial.println("  SET_TIME,YYYY,MM,DD,HH,MM,SS");
                Serial.println("  SET_FILENAME,<name>  OR  SET_FILENAME <name>");
                Serial.println("  STOP_LOG");
                Serial.println("  START_LOG");
                Serial.println("  SYNC_TIME");
                Serial.println("  STATUS");
            }
        }
    }
    
    // Ensure logging state is consistent (but do not auto-create files; user must create)
    if (loggingEnabled) {
        // If SD went offline (USB power change when another laptop connects), re-init it
        if (!sdCardReady) {
            Serial.println("WARNING: Logging enabled but SD not ready. Re-initializing SD...");
            sdCardReady = initializeSDCard();
            if (!sdCardReady) {
                Serial.println("ERROR: SD still not available. Logging paused. Create file again.");
                loggingEnabled = false;
                stoppedByUser = true;
                updateLEDStatus();
            }
        }
        // If SD is ready but file is closed, stop logging until user creates a new file
        if (sdCardReady && !logFile) {
            Serial.println("WARNING: Log file missing. Logging paused until CREATE_FILE then START_LOG.");
            loggingEnabled = false;
            stoppedByUser = true;
            updateLEDStatus();
        }
    }
    
    // Read CAN frame with minimal timeout for responsiveness
    if(ESP32Can.readFrame(rxFrame, 5)) {
        messageCount++;
        lastCanMessageTime = millis();  // Update last message time
        
        // Get current timestamp
        time_t currentTimestamp = 0;
        uint32_t currentMicros = 0;
        
        if (rtcAvailable) {
            // Use baseTime + elapsed (RTC I2C bus is disabled, so we don't read RTC in loop)
            unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
            currentTimestamp = baseTime + elapsedSeconds;
            currentMicros = micros() % 1000000; // Get microseconds from system
        } else if (baseTime != 0) {
            // Fallback: Use baseTime + elapsed time
            unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
            currentTimestamp = baseTime + elapsedSeconds;
            currentMicros = (millis() - bootMillis) * 1000; // Convert to microseconds
        }
        
        // Print timestamp and message counter
        String timestamp = getFormattedTime();
        Serial.printf("[%s][%04lu] ", timestamp.c_str(), messageCount);
        
        // Print CAN ID in hex format
        if(rxFrame.extd) {
            // Extended frame (29-bit ID)
            Serial.printf("%08X:", rxFrame.identifier);
        } else {
            // Standard frame (11-bit ID)
            Serial.printf("%03X:", rxFrame.identifier);
        }
        
        // Print data bytes in hex format
        for(int i = 0; i < rxFrame.data_length_code; i++) {
            Serial.printf("%02X", rxFrame.data[i]);
        }
        
        // Add logging indicator
        if (loggingEnabled && sdCardReady && logFile) {
            Serial.print(" [LOG]");
        }
        
        Serial.println(); // New line after each message
        
        storeLiveFrame(rxFrame, currentTimestamp, currentMicros, timestamp);
        
        // Log to SD card if logging is enabled
        if (loggingEnabled && sdCardReady && logFile) {
            if (!writeCANMessage(rxFrame, currentTimestamp, currentMicros)) {
                Serial.println("ERROR: Failed to log message to SD card!");
                Serial.println("Logging stopped due to SD card error.");
                Serial.println("Check SD card and create a new file to continue logging.");
                loggingEnabled = false;
                stoppedByUser = true;
                closeCurrentFile();
                updateLEDStatus();
            } else {
                // Successfully logged - show indicator occasionally
                static unsigned long lastLogIndicator = 0;
                if (millis() - lastLogIndicator > 5000) {  // Every 5 seconds
                    Serial.printf("[LOG] Messages logged to: %s\n", currentLogFileName.c_str());
                    lastLogIndicator = millis();
                }
            }
        }
        
        // Update LED status periodically (every 100 messages to avoid overhead)
        if (messageCount % 100 == 0) {
            updateLEDStatus();
        }
    }
    
    // Check for SD card insertion/removal periodically (every 2 seconds)
    if (millis() - lastSDCardCheck > SD_CARD_CHECK_INTERVAL) {
        if (!sdCardReady) {
            // Try to detect and initialize SD card if not already ready
            autoDetectAndInitSDCard();
        } else {
            // If SD card is ready, verify it's still present by testing actual access
            // cardType() alone may not detect removal immediately, so we test actual write access
            bool cardRemoved = false;
            
            // Test 1: Check card type (quick check)
            uint8_t cardType = SD.cardType();
            if (cardType == CARD_NONE) {
                cardRemoved = true;
            } else {
                // Test 2: Try actual write access (most reliable test for removal)
                if (!testSDCardAccess()) {
                    // Write test failed - card likely removed
                    cardRemoved = true;
                } else if (logFile) {
                    // Card accessible, verify log file is still valid
                    // Try to flush - this will fail silently if card was removed
                    logFile.flush();
                    // Check file position to verify file is still valid
                    size_t pos = logFile.position();
                    // If we got here without error, file access worked
                }
            }
            
            if (cardRemoved) {
                Serial.println("WARNING: SD Card removed!");
                closeCurrentFile();
                sdCardReady = false;
                updateLEDStatus();
            } else if (!logFile && !cardRemoved) {
                // File closed but card still present - do NOT auto-create files.
                // Notify user to CREATE_FILE to resume logging.
                Serial.println("WARNING: Log file closed unexpectedly. To resume logging, issue CREATE_FILE with desired filename.");
                // Keep sdCardReady true but logFile remains closed until user creates a file
                updateLEDStatus();
            }
        }
        lastSDCardCheck = millis();
    }
    
    // Update LED status periodically even when no messages (check every 2 seconds)
    if (millis() - lastStatusUpdate > 2000) {
        updateLEDStatus();
        lastStatusUpdate = millis();
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
}
