# Configuration Guide - CAN Data Logger

This guide lists the key settings in `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`.

## Pin Configuration
```cpp
#define CAN_TX_PIN     17
#define CAN_RX_PIN     16

#define SD_CS_PIN      15
#define SD_MOSI_PIN    20
#define SD_MISO_PIN    19
#define SD_SCK_PIN     18
#define SD_CARD_SPEED  4000000

#define RTC_SDA_PIN    22
#define RTC_SCL_PIN    23

#define NEOPIXEL_PIN   8
#define NEOPIXEL_COUNT 1
```

## WiFi Configuration
```cpp
#define ENABLE_WIFI     1

#define WIFI_AP_SSID    "CAN_Data_Logger"
#define WIFI_AP_PASS    "CANDataLogger123"
#define WIFI_AP_IP      IPAddress(192, 168, 10, 1)
#define WIFI_AP_GATEWAY IPAddress(192, 168, 10, 1)
#define WIFI_AP_SUBNET  IPAddress(255, 255, 255, 0)

#define WIFI_STA_SSID   "AkashGanga"
#define WIFI_STA_PASS   "Naxatra2025"
```

Notes:
- To disable STA, set `WIFI_STA_SSID` to an empty string or "YOUR_STA_SSID".
- Set `ENABLE_WIFI` to 0 to disable WiFi and the web server.

## Time / NTP
```cpp
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET_SEC  19800
#define DST_OFFSET_SEC  0
```

## CAN Bus
```cpp
#define CAN_SPEED_KBPS 500
#define CAN_RX_QUEUE_LEN 256
```

Supported speeds in firmware: 125, 250, 500, 800, 1000 kbps.

### CAN Filtering
`trackedCANIDs[]` exists in code but filtering is disabled. The logger currently logs all CAN frames. To filter, add a check in the receive loop and only log desired IDs.

## SD Card / Logging
```cpp
#define MAX_FILE_SIZE  10485760
#define LOG_FILE_PREFIX "CAN_LOG_"
#define FLUSH_INTERVAL 20
```

- Logs are stored under `/CAN_Logged_Data/` on the SD card.
- A new file is created when the size exceeds `MAX_FILE_SIZE`.

## Live Data Buffer (Web UI)
```cpp
#define LIVE_BUFFER_SIZE 200
#define LIVE_DEFAULT_LIMIT 50
```

## IMU (ADXL345)
```cpp
#define IMU_SAMPLE_INTERVAL_US 1000
#define IMU_CALIBRATION_SAMPLES 100
```

IMU shares the same I2C bus as the RTC (SDA/SCL).

## Encryption / File Format
```cpp
#define NXT_LOG_SIGNATURE "NXTLOG"
#define NXT_LOG_VERSION   1
#define NXT_HEADER_SIZE   16

const uint8_t ENCRYPTION_KEY[16] = {
    0x3A, 0x7C, 0xB5, 0x19,
    0xE4, 0x58, 0xC1, 0x0D,
    0x92, 0xAF, 0x63, 0x27,
    0xFE, 0x34, 0x88, 0x4B
};
```

If you change the key, you must update all decoders to match.
