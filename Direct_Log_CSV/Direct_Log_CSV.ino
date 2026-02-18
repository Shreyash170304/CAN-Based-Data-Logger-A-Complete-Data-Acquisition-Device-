/*
 * ESP32-C6 Pico CAN Data Logger (SD + CAN only)
 * Hardware: ESP32-C6 Pico (Smartelex), TJA1050 CAN Transceiver, SD Card Module
 *
 * Features:
 * - Logs raw CAN frames to CSV with DBC-aligned decoded signals (no encryption)
 * - No RTC, GPS, IMU, or Web Server
 * - LED status indicators for init/logging states
 */

#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <cstring>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Pin definitions (match existing hardware wiring)
#define CAN_TX_PIN     17
#define CAN_RX_PIN     16

#define SD_CS_PIN      15
#define SD_MOSI_PIN    20
#define SD_MISO_PIN    19
#define SD_SCK_PIN     18
#define SD_CARD_SPEED  8000000  // 8MHz SPI (faster logging)

#define NEOPIXEL_PIN   8
#define NEOPIXEL_COUNT 1
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Logging configuration
#define MAX_FILE_SIZE  10485760  // 10MB
#define LOG_FILE_PREFIX "CAN_LOG_"
#define FLUSH_INTERVAL 100

// CAN configuration
#define CAN_SPEED_KBPS 500
#define CAN_RX_QUEUE_LEN 512
int currentCanSpeedKbps = CAN_SPEED_KBPS;

// WiFi / OTA configuration
#define ENABLE_WIFI 1
#define ENABLE_OTA  1

#define WIFI_AP_SSID    "ESP_Logger"
#define WIFI_AP_PASS    "ESPLogger123"
#define WIFI_AP_IP      IPAddress(192, 168, 10, 1)
#define WIFI_AP_GATEWAY IPAddress(192, 168, 10, 1)
#define WIFI_AP_SUBNET  IPAddress(255, 255, 255, 0)

#define WIFI_STA_SSID   "AkashGanga"
#define WIFI_STA_PASS   "Naxatra2025"

#define OTA_HOSTNAME    "esp-logger"
#define OTA_PORT        3232

// CSV logging (no encryption)

// LED Status Colors
#define LED_RED       pixels.Color(255, 0, 0)
#define LED_BLUE      pixels.Color(0, 0, 255)
#define LED_GREEN     pixels.Color(0, 255, 0)
#define LED_YELLOW    pixels.Color(255, 165, 0)
#define LED_MAGENTA   pixels.Color(255, 0, 255)

// System State Variables
File logFile;
String currentLogFileName;
unsigned long currentFileSize = 0;
bool sdCardReady = false;
bool canInitialized = false;
bool dataTransferActive = false;
bool wifiConnected = false;
bool otaReady = false;
unsigned long lastDataReceivedTime = 0;
const unsigned long DATA_TIMEOUT_MS = 2000;
unsigned long messageCount = 0;
uint8_t flushCounter = 0;
#define SERIAL_FRAME_DEBUG 1
#define SERIAL_PRINT_EVERY_N 1

// Base time from compile time (no RTC)
time_t baseTime = 0;
unsigned long bootMillis = 0;

// CAN Frame structure
struct CanFrame {
    uint32_t identifier;
    bool extd;
    bool rtr;
    uint8_t data_length_code;
    uint8_t data[8];
};

// Function prototypes
bool initializeSDCard();
bool testSDCardAccess();
bool autoDetectAndInitSDCard();
bool createNewLogFile();
bool writeFileHeader();
bool writeCANMessage(const CanFrame& frame, time_t timestamp, uint32_t micros);
String generateLogFileName();
void closeCurrentFile();
bool initCAN();
bool initCANWithSpeed(int kbps);
void updateSystemLED();
bool initializeWiFi();
bool initializeOTA();

// DBC bit extraction helpers (Motorola / big-endian, as used by CJ_power_Id_2 New.dbc)
uint32_t extractBitsBE(const uint8_t* data, uint16_t startBit, uint8_t length) {
    uint32_t value = 0;
    if (length == 0) {
        return 0;
    }

    int32_t byteIndex = startBit / 8;
    int32_t bitInByte = startBit % 8;

    for (uint8_t i = 0; i < length; i++) {
        if (byteIndex < 0 || byteIndex >= 8) {
            break;
        }
        uint8_t bit = (data[byteIndex] >> bitInByte) & 0x01;
        value = (value << 1) | bit;

        if (bitInByte == 0) {
            byteIndex += 1;
            bitInByte = 7;
        } else {
            bitInByte -= 1;
        }
    }

    return value;
}

float applyScaleOffset(uint32_t raw, float scale, float offset) {
    return (raw * scale) + offset;
}

void appendFloatField(String& line, float value, uint8_t decimals) {
    line += String(value, (unsigned int)decimals);
}

void setLED(uint32_t color) {
    pixels.setPixelColor(0, color);
    pixels.show();
}

void initializeBaseTime() {
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

// ==================== WIFI FUNCTIONS ====================
#if ENABLE_WIFI
bool initializeWiFi() {
    Serial.println("Initializing WiFi (AP + STA) for OTA...");

    WiFi.mode(WIFI_AP_STA);

    if (!WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET)) {
        Serial.println("ERROR: Failed to configure WiFi AP!");
        return false;
    }

    if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS)) {
        Serial.println("ERROR: Failed to start WiFi AP!");
        return false;
    }

    Serial.print("WiFi AP started: ");
    Serial.println(WIFI_AP_SSID);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("Password: ");
    Serial.println(WIFI_AP_PASS);

    if (strlen(WIFI_STA_SSID) > 0 && strcmp(WIFI_STA_SSID, "YOUR_STA_SSID") != 0) {
        Serial.print("Connecting to STA network: ");
        Serial.println(WIFI_STA_SSID);
        WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);

        unsigned long startAttempt = millis();
        const unsigned long connectTimeout = 8000;
        while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < connectTimeout) {
            delay(200);
            yield();
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("[WiFi] STA connection: SUCCESS");
            Serial.print("[WiFi] STA IP: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("[WiFi] STA connection failed (AP-only mode)");
        }
    } else {
        Serial.println("[WiFi] No STA configured (AP-only mode)");
    }

    Serial.println("[WiFi] WiFi setup complete");
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

// ==================== SD CARD FUNCTIONS ====================
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
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    delay(100);

    if (!SD.begin(SD_CS_PIN, SPI, SD_CARD_SPEED)) {
        Serial.println("ERROR: SD Card initialization failed!");
        Serial.println("SD Card Status: NOT DETECTED");
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("ERROR: No SD card found!");
        return false;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) Serial.println("MMC");
    else if (cardType == CARD_SD) Serial.println("SDSC");
    else if (cardType == CARD_SDHC) Serial.println("SDHC");
    else Serial.println("UNKNOWN");

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    Serial.println("Testing SD card access...");
    if (!testSDCardAccess()) {
        Serial.println("ERROR: SD Card authentication failed!");
        return false;
    }
    Serial.println("SD Card access test: PASSED");
    Serial.println("SD Card initialized successfully!");
    return true;
}

bool autoDetectAndInitSDCard() {
    if (sdCardReady) return true;
    if (SD.begin(SD_CS_PIN, SPI, SD_CARD_SPEED)) {
        uint8_t cardType = SD.cardType();
        if (cardType != CARD_NONE && testSDCardAccess()) {
            sdCardReady = true;
            Serial.println("SD Card auto-detected and initialized!");
            return true;
        }
    }
    return false;
}

String generateLogFileName() {
    struct tm* timeinfo = localtime(&baseTime);
    char fileName[64];
    sprintf(fileName, "/CAN_Logged_Data/%s%04d%02d%02d_%02d%02d%02d.CSV",
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
        logFile.flush();
        logFile.close();
        delay(100);
    }

    if (!SD.exists("/CAN_Logged_Data")) {
        Serial.println("Creating CAN_Logged_Data folder...");
        if (!SD.mkdir("/CAN_Logged_Data")) {
            Serial.println("ERROR: Failed to create CAN_Logged_Data folder!");
            return false;
        }
    }

    currentLogFileName = generateLogFileName();
    Serial.print("Creating new log file: ");
    Serial.println(currentLogFileName);

    logFile = SD.open(currentLogFileName, FILE_WRITE);
    if (!logFile) {
        Serial.println("ERROR: Failed to open log file!");
        return false;
    }

    if (!writeFileHeader()) {
        Serial.println("ERROR: Failed to write file header!");
        logFile.close();
        return false;
    }

    logFile.flush();
    currentFileSize = logFile.size();
    Serial.print("Log file ready. Size: ");
    Serial.print(currentFileSize);
    Serial.println(" bytes");
    return true;
}

bool writeFileHeader() {
    if (!logFile) return false;
    String headerLine = "Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC";
    for (int i = 0; i < 8; i++) {
        headerLine += ",Data" + String(i);
    }
    headerLine += ",Message";
    headerLine += ",MCU_Status.Motor_speed";
    headerLine += ",MCU_Status.Gear_status";
    headerLine += ",MCU_Status.Status_feedback1";
    headerLine += ",MCU_Status.Status_feedback2";
    headerLine += ",MCU_Status.Status_feedback3";
    headerLine += ",MCU_Status.Power_mode";
    headerLine += ",MCU_Status.Handle_opening";
    headerLine += ",MCU_Temperatures.Controller_temp";
    headerLine += ",MCU_Temperatures.Motor_temp";
    headerLine += ",MCU_Temperatures.Bus_current";
    headerLine += ",MCU_Temperatures.Bus_voltage";
    headerLine += ",MCU_Temperatures.Phase_current_RMS";
    headerLine += ",MCU_SpeedMileage.Speed";
    headerLine += ",MCU_SpeedMileage.Wheel_circumference";
    headerLine += ",MCU_SpeedMileage.Subtotal_mileage";
    headerLine += ",MCU_SpeedMileage.Miles_remaining";
    headerLine += ",MCU_SpeedMileage.Obligate";
    headerLine += ",MCU_TotalMileage.Total_mileage";
    headerLine += ",MCU_Version.Vendor_code";
    headerLine += ",MCU_Version.HW_version";
    headerLine += ",MCU_Version.SW_version";
    headerLine += ",VCU_Commands.Turnbar_percentage";
    headerLine += ",VCU_Commands.Gear_commands";
    headerLine += ",VCU_Commands.Function_switch";
    headerLine += ",MCU_current.Id_current";
    headerLine += ",MCU_current.Iq_current";
    headerLine += ",Torquesensor.Torque";
    headerLine += ",Torquesensor.Speed";

    if (logFile.println(headerLine) <= 0) {
        Serial.println("ERROR: Failed to write CSV header");
        return false;
    }
    logFile.flush();
    currentFileSize = logFile.size();
    return true;
}

bool writeCANMessage(const CanFrame& frame, time_t timestamp, uint32_t micros) {
    if (!sdCardReady) {
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

    if (currentFileSize > MAX_FILE_SIZE) {
        Serial.println("File size limit reached - creating new file...");
        if (!createNewLogFile()) {
            Serial.println("ERROR: Failed to create new log file when size limit reached!");
            return false;
        }
    }

    struct tm* timeinfo = localtime(&timestamp);
    char timeStr[32];
    sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    String line;
    line.reserve(256);
    line += timeStr;
    line += ",";
    line += String((unsigned long)timestamp);
    line += ",";
    line += String((unsigned long)micros);
    line += ",";

    line += String(frame.identifier);
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
            line += String(frame.data[i]);
        } else {
            line += "0";
        }
    }

    const char* messageName = "UNKNOWN";
    float mcu_status_motor_speed = 0.0f;
    float mcu_status_gear_status = 0.0f;
    float mcu_status_status1 = 0.0f;
    float mcu_status_status2 = 0.0f;
    float mcu_status_status3 = 0.0f;
    float mcu_status_power_mode = 0.0f;
    float mcu_status_handle_opening = 0.0f;

    float mcu_temp_controller = 0.0f;
    float mcu_temp_motor = 0.0f;
    float mcu_temp_bus_current = 0.0f;
    float mcu_temp_bus_voltage = 0.0f;
    float mcu_temp_phase_rms = 0.0f;

    float mcu_speed_speed = 0.0f;
    float mcu_speed_wheel = 0.0f;
    float mcu_speed_subtotal = 0.0f;
    float mcu_speed_remaining = 0.0f;
    float mcu_speed_obligate = 0.0f;

    float mcu_total_mileage = 0.0f;

    float mcu_ver_vendor = 0.0f;
    float mcu_ver_hw = 0.0f;
    float mcu_ver_sw = 0.0f;

    float vcu_turnbar = 0.0f;
    float vcu_gear_cmd = 0.0f;
    float vcu_func = 0.0f;

    float mcu_id_current = 0.0f;
    float mcu_iq_current = 0.0f;

    float torque_sensor_torque = 0.0f;
    float torque_sensor_speed = 0.0f;

    switch (frame.identifier) {
        case 0x411: {  // 1041 MCU_Status
            messageName = "MCU_Status";
            mcu_status_motor_speed = applyScaleOffset(extractBitsBE(frame.data, 7, 16), 1.0f, 0.0f);
            mcu_status_gear_status = applyScaleOffset(extractBitsBE(frame.data, 23, 8), 1.0f, 0.0f);
            mcu_status_status1 = applyScaleOffset(extractBitsBE(frame.data, 31, 8), 1.0f, 0.0f);
            mcu_status_status2 = applyScaleOffset(extractBitsBE(frame.data, 39, 8), 1.0f, 0.0f);
            mcu_status_status3 = applyScaleOffset(extractBitsBE(frame.data, 47, 8), 1.0f, 0.0f);
            mcu_status_power_mode = applyScaleOffset(extractBitsBE(frame.data, 55, 8), 1.0f, 0.0f);
            mcu_status_handle_opening = applyScaleOffset(extractBitsBE(frame.data, 63, 8), 1.0f, 0.0f);
            break;
        }
        case 0x412: {  // 1042 MCU_Temperatures
            messageName = "MCU_Temperatures";
            mcu_temp_controller = applyScaleOffset(extractBitsBE(frame.data, 7, 8), 1.0f, -40.0f);
            mcu_temp_motor = applyScaleOffset(extractBitsBE(frame.data, 15, 8), 1.0f, -40.0f);
            mcu_temp_bus_current = applyScaleOffset(extractBitsBE(frame.data, 23, 16), 0.1f, -1000.0f);
            mcu_temp_bus_voltage = applyScaleOffset(extractBitsBE(frame.data, 39, 16), 1.0f, 0.0f);
            mcu_temp_phase_rms = applyScaleOffset(extractBitsBE(frame.data, 55, 16), 1.0f, 0.0f);
            break;
        }
        case 0x231: {  // 561 MCU_SpeedMileage
            messageName = "MCU_SpeedMileage";
            mcu_speed_speed = applyScaleOffset(extractBitsBE(frame.data, 7, 8), 1.0f, 0.0f);
            mcu_speed_wheel = applyScaleOffset(extractBitsBE(frame.data, 15, 8), 1.0f, 0.0f);
            mcu_speed_subtotal = applyScaleOffset(extractBitsBE(frame.data, 23, 16), 0.1f, 0.0f);
            mcu_speed_remaining = applyScaleOffset(extractBitsBE(frame.data, 39, 16), 0.1f, 0.0f);
            mcu_speed_obligate = applyScaleOffset(extractBitsBE(frame.data, 55, 16), 1.0f, 0.0f);
            break;
        }
        case 0x232: {  // 562 MCU_TotalMileage
            messageName = "MCU_TotalMileage";
            mcu_total_mileage = applyScaleOffset(extractBitsBE(frame.data, 7, 32), 1.0f, 0.0f);
            break;
        }
        case 0x440: {  // 1088 MCU_Version
            messageName = "MCU_Version";
            mcu_ver_vendor = applyScaleOffset(extractBitsBE(frame.data, 7, 32), 1.0f, 0.0f);
            mcu_ver_hw = applyScaleOffset(extractBitsBE(frame.data, 39, 16), 1.0f, 0.0f);
            mcu_ver_sw = applyScaleOffset(extractBitsBE(frame.data, 55, 16), 1.0f, 0.0f);
            break;
        }
        case 0x415: {  // 1045 VCU_Commands
            messageName = "VCU_Commands";
            vcu_turnbar = applyScaleOffset(extractBitsBE(frame.data, 7, 8), 1.0f, -100.0f);
            vcu_gear_cmd = applyScaleOffset(extractBitsBE(frame.data, 15, 8), 1.0f, 0.0f);
            vcu_func = applyScaleOffset(extractBitsBE(frame.data, 23, 8), 1.0f, 0.0f);
            break;
        }
        case 0x413: {  // 1043 MCU_current
            messageName = "MCU_current";
            mcu_id_current = applyScaleOffset(extractBitsBE(frame.data, 7, 16), 1.0f, 0.0f);
            mcu_iq_current = applyScaleOffset(extractBitsBE(frame.data, 23, 16), 1.0f, 0.0f);
            break;
        }
        case 0x432: {  // 1074 Torquesensor
            messageName = "Torquesensor";
            torque_sensor_torque = applyScaleOffset(extractBitsBE(frame.data, 7, 16), 0.1f, 0.0f);
            torque_sensor_speed = applyScaleOffset(extractBitsBE(frame.data, 23, 16), 1.0f, 0.0f);
            break;
        }
        default:
            break;
    }

    line += ",";
    line += messageName;

    line += ",";
    appendFloatField(line, mcu_status_motor_speed, 0);
    line += ",";
    appendFloatField(line, mcu_status_gear_status, 0);
    line += ",";
    appendFloatField(line, mcu_status_status1, 0);
    line += ",";
    appendFloatField(line, mcu_status_status2, 0);
    line += ",";
    appendFloatField(line, mcu_status_status3, 0);
    line += ",";
    appendFloatField(line, mcu_status_power_mode, 0);
    line += ",";
    appendFloatField(line, mcu_status_handle_opening, 0);

    line += ",";
    appendFloatField(line, mcu_temp_controller, 0);
    line += ",";
    appendFloatField(line, mcu_temp_motor, 0);
    line += ",";
    appendFloatField(line, mcu_temp_bus_current, 1);
    line += ",";
    appendFloatField(line, mcu_temp_bus_voltage, 0);
    line += ",";
    appendFloatField(line, mcu_temp_phase_rms, 0);

    line += ",";
    appendFloatField(line, mcu_speed_speed, 0);
    line += ",";
    appendFloatField(line, mcu_speed_wheel, 0);
    line += ",";
    appendFloatField(line, mcu_speed_subtotal, 1);
    line += ",";
    appendFloatField(line, mcu_speed_remaining, 1);
    line += ",";
    appendFloatField(line, mcu_speed_obligate, 0);

    line += ",";
    appendFloatField(line, mcu_total_mileage, 0);

    line += ",";
    appendFloatField(line, mcu_ver_vendor, 0);
    line += ",";
    appendFloatField(line, mcu_ver_hw, 0);
    line += ",";
    appendFloatField(line, mcu_ver_sw, 0);

    line += ",";
    appendFloatField(line, vcu_turnbar, 0);
    line += ",";
    appendFloatField(line, vcu_gear_cmd, 0);
    line += ",";
    appendFloatField(line, vcu_func, 0);

    line += ",";
    appendFloatField(line, mcu_id_current, 0);
    line += ",";
    appendFloatField(line, mcu_iq_current, 0);

    line += ",";
    appendFloatField(line, torque_sensor_torque, 1);
    line += ",";
    appendFloatField(line, torque_sensor_speed, 0);

    if (logFile.println(line) <= 0) {
        Serial.println("ERROR: Failed to write CSV line");
        return false;
    }
    currentFileSize = logFile.size();

    // Serial output with decoded values (DBC-aligned)
#if SERIAL_FRAME_DEBUG
    if ((SERIAL_PRINT_EVERY_N <= 1) || (messageCount % SERIAL_PRINT_EVERY_N == 0)) {
        if (strcmp(messageName, "MCU_Status") == 0) {
            Serial.printf("MCU_Status: Motor_speed=%.0f rpm, Gear=%.0f, Status1=%.0f, Status2=%.0f, Status3=%.0f, Power_mode=%.0f, Handle_opening=%.0f\n",
                          mcu_status_motor_speed, mcu_status_gear_status, mcu_status_status1,
                          mcu_status_status2, mcu_status_status3, mcu_status_power_mode,
                          mcu_status_handle_opening);
        } else if (strcmp(messageName, "MCU_Temperatures") == 0) {
            Serial.printf("MCU_Temperatures: Controller_temp=%.0f C, Motor_temp=%.0f C, Bus_current=%.1f A, Bus_voltage=%.0f V, Phase_RMS=%.0f A\n",
                          mcu_temp_controller, mcu_temp_motor, mcu_temp_bus_current, mcu_temp_bus_voltage, mcu_temp_phase_rms);
        } else if (strcmp(messageName, "MCU_SpeedMileage") == 0) {
            Serial.printf("MCU_SpeedMileage: Speed=%.0f km/h, Wheel=%.0f cm, Subtotal=%.1f km, Remaining=%.1f km, Obligate=%.0f\n",
                          mcu_speed_speed, mcu_speed_wheel, mcu_speed_subtotal, mcu_speed_remaining, mcu_speed_obligate);
        } else if (strcmp(messageName, "MCU_TotalMileage") == 0) {
            Serial.printf("MCU_TotalMileage: Total=%.0f km\n", mcu_total_mileage);
        } else if (strcmp(messageName, "MCU_Version") == 0) {
            Serial.printf("MCU_Version: Vendor=%.0f, HW=%.0f, SW=%.0f\n",
                          mcu_ver_vendor, mcu_ver_hw, mcu_ver_sw);
        } else if (strcmp(messageName, "VCU_Commands") == 0) {
            Serial.printf("VCU_Commands: Turnbar=%.0f %%, Gear_cmd=%.0f, Function=%.0f\n",
                          vcu_turnbar, vcu_gear_cmd, vcu_func);
        } else if (strcmp(messageName, "MCU_current") == 0) {
            Serial.printf("MCU_current: Id=%.0f A, Iq=%.0f A\n", mcu_id_current, mcu_iq_current);
        } else if (strcmp(messageName, "Torquesensor") == 0) {
            Serial.printf("Torquesensor: Torque=%.1f Nm, Speed=%.0f RPM\n", torque_sensor_torque, torque_sensor_speed);
        } else {
            Serial.printf("UNKNOWN: ID=%lu DLC=%u Data:", (unsigned long)frame.identifier, frame.data_length_code);
            for (int i = 0; i < frame.data_length_code && i < 8; i++) {
                Serial.printf(" %u", frame.data[i]);
            }
            Serial.println();
        }
    }
#endif

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

void closeCurrentFile() {
    if (logFile) {
        logFile.flush();
        logFile.close();
        logFile = File();
    }
}

// ==================== CAN FUNCTIONS ====================
bool initCANWithSpeed(int kbps) {
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
        Serial.print("ERROR: CAN Bus initialization failed! Error Code: ");
        Serial.println(result);
        return false;
    }

    result = twai_start();
    if (result != ESP_OK) {
        twai_driver_uninstall();
        Serial.print("ERROR: CAN Bus start failed! Error Code: ");
        Serial.println(result);
        return false;
    }

    canInitialized = true;
    currentCanSpeedKbps = kbps;
    Serial.println("CAN Bus initialized successfully!");
    return true;
}

bool initCAN() {
    return initCANWithSpeed(CAN_SPEED_KBPS);
}

void updateSystemLED() {
    bool allReady = sdCardReady && canInitialized;
    if (!allReady) {
        setLED(LED_RED);
        return;
    }
    if (dataTransferActive) {
        setLED(LED_MAGENTA);
        return;
    }
    if (messageCount > 0 && (millis() - lastDataReceivedTime) >= DATA_TIMEOUT_MS) {
        setLED(LED_YELLOW);
        return;
    }
    setLED(LED_GREEN);
}

// ==================== SETUP / LOOP ====================
void setup() {
    Serial.begin(115200);
    delay(200);
    unsigned long serialStart = millis();
    while (!Serial && (millis() - serialStart) < 3000) {
        delay(10);
    }
    Serial.println();
    Serial.println("Serial OK. Starting ESP32 CAN Logger...");
    pixels.begin();
    setLED(LED_BLUE);

    Serial.println("========================================");
    Serial.println("ESP32 CAN Logger (SD + CAN only)");
    Serial.println("========================================");

    Serial.println("Step 0: Initializing base time (compile time)...");
    initializeBaseTime();
    Serial.printf("Base time (unix): %lu\n", (unsigned long)baseTime);

    #if ENABLE_WIFI
    Serial.println("Step 0.5: Initializing WiFi for OTA...");
    if (initializeWiFi()) {
        #if ENABLE_OTA
        initializeOTA();
        #endif
    }
    #endif

    Serial.println("Step 1: Initializing SD Card...");
    sdCardReady = initializeSDCard();
    Serial.println(sdCardReady ? "SD Card: READY" : "SD Card: FAILED");

    Serial.println("Step 2: Initializing CAN Bus...");
    canInitialized = initCAN();
    Serial.println(canInitialized ? "CAN Bus: READY" : "CAN Bus: FAILED");

    if (sdCardReady && canInitialized) {
        setLED(LED_GREEN);
        Serial.println("System READY. Waiting for CAN data...");
    } else {
        setLED(LED_RED);
        Serial.println("System NOT READY. Check SD/CAN.");
    }
    Serial.println("Setup complete.");
    Serial.flush();
}

void loop() {
    #if ENABLE_WIFI && ENABLE_OTA
    ArduinoOTA.handle();
    #endif

    if (canInitialized) {
        twai_message_t message;
        uint8_t processedCount = 0;
        const uint16_t maxProcessPerCall = 400;

        while (processedCount < maxProcessPerCall && twai_receive(&message, pdMS_TO_TICKS(1)) == ESP_OK) {
            uint32_t canId = message.identifier;
            messageCount++;
            lastDataReceivedTime = millis();
            dataTransferActive = true;

            if (sdCardReady && !logFile) {
                createNewLogFile();
            }

            time_t currentTimestamp = 0;
            uint32_t currentMicros = 0;
            unsigned long elapsedSeconds = (millis() - bootMillis) / 1000;
            currentTimestamp = baseTime + elapsedSeconds;
            currentMicros = micros() % 1000000;

            CanFrame rxFrame;
            rxFrame.identifier = message.identifier;
            rxFrame.extd = message.extd;
            rxFrame.rtr = message.rtr;
            rxFrame.data_length_code = message.data_length_code;
            memcpy(rxFrame.data, message.data, 8);
            if (rxFrame.data_length_code < 8) {
                for (int i = rxFrame.data_length_code; i < 8; i++) {
                    rxFrame.data[i] = 0;
                }
            }

            if (sdCardReady) {
                if (!writeCANMessage(rxFrame, currentTimestamp, currentMicros)) {
                    Serial.printf("ERROR: Failed to log message %lu (CAN ID: %lu)\n", messageCount, canId);
                    sdCardReady = false;
                }
            }

            setLED(LED_MAGENTA);
            processedCount++;
        }

        if (dataTransferActive && (millis() - lastDataReceivedTime) >= DATA_TIMEOUT_MS) {
            dataTransferActive = false;
            updateSystemLED();
            Serial.println("Logging stopped (timeout).");
        }
    } else {
        updateSystemLED();
    }

    // No RX/TX stats printed; serial output focuses on decoded data only.

    static unsigned long lastSDCardCheck = 0;
    static unsigned long lastCANCheck = 0;
    if (millis() - lastSDCardCheck > 2000) {
        if (sdCardReady) {
            uint8_t cardType = SD.cardType();
            if (cardType == CARD_NONE) {
                Serial.println("WARNING: SD Card removed!");
                closeCurrentFile();
                sdCardReady = false;
                dataTransferActive = false;
                updateSystemLED();
            }
        } else {
            if (autoDetectAndInitSDCard()) {
                Serial.println("SD Card re-initialized.");
                updateSystemLED();
            }
        }
        lastSDCardCheck = millis();
    }

    if (millis() - lastCANCheck > 1000) {
        if (canInitialized) {
            twai_status_info_t statusInfo;
            if (twai_get_status_info(&statusInfo) == ESP_OK) {
                if (statusInfo.state == TWAI_STATE_BUS_OFF || statusInfo.state == TWAI_STATE_STOPPED) {
                    Serial.println("WARNING: CAN bus off / stopped. Reinitializing...");
                    twai_stop();
                    twai_driver_uninstall();
                    canInitialized = false;
                    dataTransferActive = false;
                    updateSystemLED();
                }
            }
        } else {
            Serial.println("Attempting CAN re-initialization...");
            if (initCAN()) {
                Serial.println("CAN Bus re-initialized.");
                updateSystemLED();
            }
        }
        lastCANCheck = millis();
    }

    delayMicroseconds(100);
}
