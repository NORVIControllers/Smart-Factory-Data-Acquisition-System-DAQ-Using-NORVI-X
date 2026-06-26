/*  Config.h  -  Site/commissioning settings for the NORVI X DAQ node.
 *  Change these to deploy a node. The I/O tag list lives in IOList.h.
 */
#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>

// ===== IDENTITY ============================================================
#define FW_VERSION   "1.1.0"
#define SITE_ID      "plant1"
#define LINE_ID      "lineA"
#define DEVICE_ID    "norvi-x-daq-01"
#define TOPIC_ROOT   "norvi/" SITE_ID "/" LINE_ID "/" DEVICE_ID

// ===== TRANSPORT  (1 = W5500 Ethernet / NORVI X1,  0 = Wi-Fi) ==============
#define USE_ETHERNET 0
#define WIFI_SSID    ""
#define WIFI_PASS    ""
#define USE_DHCP     1
static const uint8_t ETH_MAC[6] = { 0xDE,0xAD,0xBE,0xEF,0xFE,0xED };

// ===== MQTT BROKER =========================================================
#define MQTT_HOST    ""
#define MQTT_PORT    1883
#define MQTT_USER    ""                 // "" = anonymous
#define MQTT_PASS    ""
#define MQTT_KEEPALIVE 30

// ===== RATES (ms) ==========================================================
#define DI_POLL_MS     100        // digital scan
#define AI_POLL_MS     500        // analogue scan
#define PUBLISH_MS     5000       // <<< MQTT SEND INTERVAL (the only send-rate knob)
#define DISPLAY_MS     500
#define DI_DEBOUNCE_MS 25

// ===== RECONNECT / RETRY (ms) ==============================================
#define WIFI_RETRY_MS  5000       // Wi-Fi/Ethernet reconnect attempt period
#define MQTT_RETRY_MS  3000       // MQTT reconnect attempt period
#define SD_RETRY_MS    10000      // SD re-init attempt period if not mounted

// ===== TIME ================================================================
#define NTP_SERVER     "pool.ntp.org"
#define NTP_RESYNC_MS  (6UL*3600UL*1000UL)

// ===== STORE-AND-FORWARD (offline buffering to SD) =========================
#define ENABLE_STORE_FWD 1
#define BUFFER_FILE      "/daq_buffer.jsonl"
#define FLUSH_MAX_LINES  25
#define BUFFER_MAX_BYTES (2UL*1024UL*1024UL)

// ===== DISPLAY (set 0 to skip TFT/CST816S) =================================
#define ENABLE_DISPLAY 1

// ===== MODBUS (set 0 if no RS-485 instruments are wired) ===================
#define ENABLE_MODBUS  1
#define MB_MAX_ERRORS  2          // consecutive fails before a slave is parked
#define MB_BACKOFF_MS  30000      // how long a dead slave is parked before retry

// ===== CPU MODULE PIN MAP  (NORVI X-CPU-ESPS3-X1) ==========================
#define I2C_SDA  8
#define I2C_SCL  9
#define RS485_RX 16
#define RS485_TX 15
#define RS485_DE 41
#define RS485_BAUD 9600
#define ETH_CS   1
#define SPI_SCLK 12
#define SPI_MISO 13
#define SPI_MOSI 11
#define SD_CS    42
#define SD_SPI_HZ 20000000UL
#define PCA9539_RST 38
#define ADDR_PCA9539 0x75
#define ADDR_PCA9536 0x41
#define IO_PB1 0
#define LED_RUN 1
#define LED_NET 2
#define IO_PB3 3

// ===== EXPANSION MODULE I2C ADDRESSES ======================================
static const uint8_t ADS_ADDR[2] = { 0x48, 0x49 };   // AI4  (ADS1115)
static const uint8_t PCA_ADDR[2] = { 0x27, 0x26 };   // DI16 (PCA9555)

// ===== CAPACITY / BUFFERS ==================================================
#define MAX_MODULES 2
#define PAYLOAD_BUF 2048

// ===== ANALOGUE CONVERSION =================================================
#define AI_MA_FACTOR    (4.096f/3269.826f)
#define AI_WIREBREAK_MA 3.6f
#define AI_V_DIVIDER    2.5f
#define AI_MODE_420MA 0
#define AI_MODE_010V  1

// ===== MODBUS TYPES / FUNCTION CODES =======================================
#define MB_U16 0
#define MB_S16 1
#define MB_U32 2
#define MB_FLOAT32 3
#define MB_FC_HOLDING 3
#define MB_FC_INPUT   4

// ===== DATA MODEL (shared by .ino and IOList.h) ============================
struct AiChannel {
  bool enabled; const char* key; const char* unit;
  uint8_t module; uint8_t ch; uint8_t mode; float engMin; float engMax;
  float raw; float eng; bool fault;
};
struct DiChannel {
  bool enabled; const char* key; uint8_t module; uint8_t pin; bool invert;
  bool state; bool candidate; uint32_t tCandidate; uint32_t count;
};
struct ModbusReg {
  bool enabled; const char* key; const char* unit;
  uint8_t slaveId; uint8_t fc; uint16_t addr; uint8_t dtype; bool wordSwap;
  float scale; float offset; uint32_t pollMs;
  float value; bool valid; uint32_t lastPoll; uint16_t errCount;
  uint32_t downUntil;   // runtime: dead-slave backoff timer (auto-zeroed)
};

#endif // CONFIG_H
