// ============================================================================
// AetherLogic IoT Gateway — Firmware for ESP32-S3
// ============================================================================
//
// Project     : AetherLogic — Real-Time Air Quality & Noise Monitoring System
//               for Industrial Aluminum Powder Coating Environments
// Component   : LoRa-to-Cloud Gateway (Central Aggregation Node)
// Platform    : ESP32-S3 (Xtensa LX7 dual-core, 512 KB SRAM)
// Framework   : Arduino-ESP32 Core v3.0.5
//               NOTE: Core v3.0.5 is required — earlier versions exhibit SPI
//               stack allocation failures when two SPI buses operate concurrently.
// Version     : 2.6.0
//
// ----------------------------------------------------------------------------
// SYSTEM ARCHITECTURE
// ----------------------------------------------------------------------------
//
// This gateway serves as the central bridge in a star-topology IoT network:
//
//   [Sensor Node 1] ──┐
//   [Sensor Node 2] ──┤── LoRa 868 MHz ──> [THIS GATEWAY] ── WiFi ──> MQTT/TLS
//   [Sensor Node N] ──┘                          │                     (port 8883)
//                                                 │                        │
//                                            ST7789 TFT              EMQX Broker
//                                           (local HMI)                    │
//                                                                     Telegraf
//                                                                         │
//                                                                     InfluxDB
//                                                                         │
//                                                                      Grafana
//
// Sensor nodes transmit AES-128-ECB encrypted binary packets over LoRa at
// 868 MHz (EU ISM band, compliant with ETSI EN 300 220). The gateway decrypts
// incoming packets, validates their integrity via a magic number, serializes
// the sensor readings into JSON, and forwards them to an EMQX MQTT broker
// over TLS-secured WiFi. An optional HTTP POST endpoint is also supported.
//
// The gateway features a captive portal for initial configuration, mDNS for
// local network discovery, NTP for ISO 8601 timestamping, OTA firmware
// updates with progress tracking, and a hardware watchdog for autonomous
// fault recovery.
//
// ----------------------------------------------------------------------------
// HARDWARE CONFIGURATION
// ----------------------------------------------------------------------------
//
// MCU         : ESP32-S3-WROOM-1 (selected for its dual SPI controllers and
//               sufficient SRAM for MQTT/TLS buffering and JSON serialization)
// LoRa        : Semtech SX1276 @ 868 MHz, connected via FSPI bus
//               (LLCC68 modules were evaluated but exhibited incompatibility
//               with the Arduino-ESP32 SPI driver — see project notes)
// Display     : ST7789 TFT 320x240 IPS, connected via HSPI bus
//               (FSPI and HSPI are used on separate buses to avoid SPI
//               arbitration conflicts between LoRa and display operations)
// User Input  : Tactile pushbutton on GPIO0 (short press: backlight toggle,
//               long press >3 s: factory reset of WiFi credentials)
//
// ----------------------------------------------------------------------------
// KEY FEATURES
// ----------------------------------------------------------------------------
//
//  1. AES-128-ECB decryption of incoming LoRa packets (mbedTLS)
//  2. MQTT over TLS (port 8883) with Last Will and Testament (LWT)
//  3. Configurable MQTT QoS levels (0, 1, 2) per OASIS MQTT v3.1.1 spec
//  4. Exponential backoff for MQTT reconnection (2 s initial, 60 s maximum)
//  5. ISO 8601 timestamps with timezone offset (NTP-synchronized)
//  6. Periodic gateway heartbeat via MQTT (health telemetry every 5 min)
//  7. Offline message buffering (FIFO, up to 50 messages, FIFO eviction)
//  8. OTA firmware updates with watchdog-safe progress tracking
//  9. Captive portal with HTTP Basic Authentication for web configuration
// 10. mDNS responder for local network discovery (http://s3gateway.local)
// 11. Automatic LoRa module recovery after 15-minute inactivity timeout
// 12. Dynamic heap monitoring with emergency buffer cleanup
// ============================================================================

#define FIRMWARE_VERSION "2.6.0"

// ============================================================================
// LIBRARY DEPENDENCIES
// ============================================================================
// Networking & Connectivity
#include <WiFi.h>               // ESP32 WiFi driver (STA + AP modes)
#include <WiFiClientSecure.h>   // TLS/SSL socket wrapper for secure MQTT (port 8883)
#include <WebServer.h>          // HTTP server for captive portal and configuration UI
#include <Update.h>             // ESP32 OTA (Over-The-Air) firmware update API
#include <Preferences.h>        // NVS (Non-Volatile Storage) abstraction for persistent config
#include <DNSServer.h>          // DNS server for captive portal redirection in AP mode
#include <ESPmDNS.h>            // mDNS responder — enables discovery via hostname.local

// SPI Peripherals
#include <SPI.h>                // Hardware SPI driver (dual-bus: FSPI for LoRa, HSPI for TFT)
#include <LoRa.h>               // Semtech SX1276/SX1278 LoRa transceiver driver (Sandeep Mistry)
#include <Adafruit_GFX.h>       // Graphics primitives library (fonts, shapes, text rendering)
#include <Adafruit_ST7789.h>    // ST7789 TFT display driver (240x320, SPI interface)

// Security & Protocols
#include "mbedtls/aes.h"        // AES-128-ECB decryption via mbedTLS (hardware-accelerated on ESP32)
#include <PubSubClient.h>       // MQTT v3.1.1 client library (supports QoS 0/1, LWT, keepalive)
#include <HTTPClient.h>         // HTTP client for optional REST API forwarding

// System Utilities
#include "time.h"               // POSIX time functions for NTP synchronization and timestamping
#include <vector>               // STL dynamic array — used for offline message buffer and log history
#include "esp_task_wdt.h"       // ESP-IDF Task Watchdog Timer API (Core 3.x syntax)

// ============================================================================
// GPIO PIN ASSIGNMENTS
// ============================================================================
// Two dedicated SPI buses are used to avoid bus contention between the LoRa
// transceiver (which requires low-latency access for packet reception) and the
// TFT display (which performs bulk data transfers for screen updates).
// See docs/pinouts.md for the complete wiring diagram.

// LoRa SX1276 Transceiver — FSPI Bus (SPI2)
// The FSPI bus is dedicated to LoRa to ensure uninterrupted packet reception.
#define LORA_SCK    4       // SPI Clock
#define LORA_MISO   5       // SPI Master-In Slave-Out (received data from SX1276)
#define LORA_MOSI   6       // SPI Master-Out Slave-In (commands/data to SX1276)
#define LORA_CS     7       // Chip Select (active low)
#define LORA_RST    15      // Hardware reset (active low, used for module recovery)
#define LORA_DIO0   16      // Digital I/O 0 — RxDone interrupt (packet received)

// ST7789 TFT Display — HSPI Bus (SPI3)
// The HSPI bus is dedicated to the display to allow concurrent LoRa reception.
#define TFT_SCLK    9       // SPI Clock
#define TFT_MOSI    10      // SPI Data (display is write-only, no MISO needed)
#define TFT_CS      13      // Chip Select (active low)
#define TFT_DC      12      // Data/Command selection (HIGH = data, LOW = command)
#define TFT_RST     11      // Hardware reset (active low)
#define TFT_BL      41      // Backlight enable (HIGH = on), auto-off after timeout

// User Interface — Tactile Pushbutton
// Short press (<3 s): re-activates display backlight
// Long press (>3 s):  factory reset of WiFi credentials and web password
#define BTN_PIN     0       // GPIO0 — active low with internal pull-up
#define LONG_PRESS_MS 3000  // Long press detection threshold (milliseconds)

// ============================================================================
// DEFAULT CONFIGURATION VALUES
// ============================================================================
// These defaults are used on first boot or when NVS contains no saved values.
// All parameters are user-configurable at runtime through the web portal and
// are persisted in NVS (Non-Volatile Storage) across reboots.

// WiFi Station Credentials (empty = start in AP/captive portal mode)
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""

// Web Interface Authentication
// HTTP Basic Auth protects the configuration portal and status page.
#define DEFAULT_WEB_PASSWORD "@dmin12#4"

// MQTT Broker Configuration
// Default broker: EMQX Cloud (EU-Central-1 region) with mandatory TLS on port 8883.
// The PubSubClient library implements MQTT v3.1.1 (OASIS Standard).
#define DEFAULT_MQTT_SERVER "d0a16fcc.ala.eu-central-1.emqxsl.com"
#define DEFAULT_MQTT_PORT 8883          // TLS-encrypted MQTT (IANA assigned port)
#define DEFAULT_MQTT_USER ""
#define DEFAULT_MQTT_PASS ""
#define DEFAULT_MQTT_TOPIC "lora/sensors"
#define DEFAULT_MQTT_QOS 0              // QoS 0: at-most-once (fire and forget)
#define DEFAULT_MQTT_KEEPALIVE 30       // PINGREQ interval in seconds (MQTT v3.1.1 §3.1.2.10)

// MQTT Last Will and Testament (LWT)
// The LWT mechanism (MQTT v3.1.1 §3.1.2.5) ensures the broker publishes an
// "offline" message on the status topic if the gateway disconnects unexpectedly
// (e.g., power loss, network failure). Upon successful connection, an "online"
// message is explicitly published to the same topic with the retain flag set.
#define DEFAULT_MQTT_LWT_TOPIC "aetherlogic/gateway/status"
#define DEFAULT_MQTT_LWT_MSG_OFFLINE "offline"
#define DEFAULT_MQTT_LWT_MSG_ONLINE "online"
#define DEFAULT_MQTT_LWT_QOS 1          // QoS 1: at-least-once delivery for status messages
#define DEFAULT_MQTT_LWT_RETAIN true    // Retained so new subscribers get the last known status

// Optional HTTP POST Endpoint (empty = disabled)
#define DEFAULT_HTTP_URL ""

// LoRa Encryption & Packet Validation
// AES-128-ECB is used for symmetric encryption of the 48-byte sensor packets.
// The magic number serves as a lightweight integrity check after decryption.
#define DEFAULT_AES_KEY "MySecretKey12345"   // 16-byte (128-bit) AES key
#define DEFAULT_MAGIC 0xCAFE                 // 2-byte magic for packet validation

// LoRa Radio Profile (must match the sensor node configuration)
// Profile 3 (LONG) is the default: SF10, BW 125 kHz, CR 4/6, TX 20 dBm
// Achieves approximately 8 km range in metallic industrial environments.
#define DEFAULT_LORA_PROFILE 3

// NTP Time Synchronization
// Used for ISO 8601 timestamps in JSON payloads and status monitoring.
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 7200     // UTC+2 (EET — Eastern European Time)
#define DST_OFFSET_SEC 3600     // +1 hour during EEST (Eastern European Summer Time)

// mDNS Hostname — accessible as http://s3gateway.local on the LAN
#define HOSTNAME "s3gateway"

// ============================================================================
// SYSTEM CONSTANTS
// ============================================================================

// Display Geometry
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// Custom 16-bit RGB565 Color Definitions (not provided by Adafruit library)
#define ST77XX_GRAY     0x8410      // Mid-gray for section labels
#define ST77XX_DARKGREY 0x4208      // Dark gray for divider lines

// Timing & Thresholds
#define BL_TIMEOUT      20000       // Backlight auto-off timeout (20 seconds)
#define DNS_PORT        53          // Standard DNS port for captive portal

// Offline Message Buffer
// When MQTT is unavailable, sensor packets are queued in RAM (FIFO).
// When the buffer reaches capacity, the oldest message is evicted.
#define MAX_BUFFER_SIZE 50

// MQTT Reconnection Strategy — Exponential Backoff
// Prevents network flooding during broker outages. Delay doubles on each
// failed attempt: 2s -> 4s -> 8s -> ... -> 60s (capped).
#define MQTT_RETRY_INITIAL_DELAY 2000UL     // Initial retry delay: 2 seconds
#define MQTT_RETRY_MAX_DELAY     60000UL    // Maximum retry delay: 60 seconds

// Gateway Health Heartbeat
// Publishes a JSON health report (uptime, heap, WiFi RSSI, packet count)
// to the MQTT topic aetherlogic/gateway/{gwID}/health at this interval.
#define HEARTBEAT_INTERVAL       300000UL   // 5 minutes (300,000 ms)

// Scheduled Reboot Interval
// Automatic restart every 12 hours for long-term stability (memory leaks,
// stale connections). Active in ALL operating modes (AP, normal, reconnecting).
#define REBOOT_INTERVAL          43200000UL // 12 hours (43,200,000 ms)

// ============================================================================
// LORA PACKET DATA STRUCTURE (48 Bytes, AES-128 Block-Aligned)
// ============================================================================
// Binary payload transmitted by sensor nodes over LoRa. The 48-byte size
// aligns to exactly 3 AES-128 blocks (3 x 16 bytes), enabling encryption
// without padding overhead. The sensor node encrypts this struct with
// AES-128-ECB and transmits the ciphertext. The gateway decrypts and
// validates the magic number before processing.
struct SecurePacket {
  uint16_t magic;       // Validation marker (default: 0xCAFE) — checked after decryption
  uint16_t deviceId;    // Unique sensor node identifier (1–65535)
  uint32_t packetId;    // Monotonically increasing sequence number
  float voltage;        // Battery voltage [V]
  float noise_db;       // Sound pressure level [dBA] (analog microphone via ADC)
  float temp;           // Ambient temperature [°C] (BME680)
  float hum;            // Relative humidity [%RH] (BME680)
  float pressure;       // Barometric pressure [hPa] (BME680)
  float iaq;            // Indoor Air Quality index [0–500] (BME680)
  uint16_t co2;         // CO2 concentration [ppm] (SCD40 — true NDIR, not eCO2)
  uint16_t pm1;         // PM1.0 [ug/m3] (ZH03B laser scattering)
  uint16_t pm25;        // PM2.5 [ug/m3] (ZH03B)
  uint16_t pm10;        // PM10 [ug/m3] (ZH03B)
  float tvoc;           // Total VOC [mg/m3] (BME680)
  uint8_t padding[4];   // Alignment to 48 bytes (3 AES blocks)
};

// ============================================================================
// GLOBAL OBJECTS AND PERIPHERAL INSTANCES
// ============================================================================

Preferences prefs;

// SPI Bus Instances — dedicated buses to avoid contention between LoRa and TFT
SPIClass tftSPI(HSPI);              // HSPI (SPI3) for TFT display
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, TFT_CS, TFT_DC, TFT_RST);
SPIClass loraSPI(FSPI);             // FSPI (SPI2) for LoRa transceiver

WebServer server(80);
DNSServer dnsServer;

// MQTT Client Stack — WiFiClientSecure provides TLS, PubSubClient implements MQTT v3.1.1
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);

// Offline message buffer — bounded FIFO queue, oldest messages evicted when full
std::vector<String> msgBuffer;

// Rolling log of last 5 received packets — displayed on /status web dashboard
struct MessageLog {
  String deviceName;
  int deviceId;
  int packetId;
  int rssi;             // [dBm]
  String timestamp;     // HH:MM
  float temp;           // [°C]
  float hum;            // [%RH]
  int co2;              // [ppm]
};
std::vector<MessageLog> lastMessages;
bool loraInitOK = false;

// Runtime configuration — loaded from NVS by loadSettings(), editable via web portal
String cfg_ssid, cfg_pass;
String cfg_web_password;
String cfg_mqtt_server, cfg_mqtt_user, cfg_mqtt_pass, cfg_mqtt_topic;
int    cfg_mqtt_port;
uint8_t cfg_mqtt_qos;
uint16_t cfg_mqtt_keepalive;         // [seconds], range 10–300
String cfg_mqtt_lwt_topic, cfg_mqtt_lwt_msg_offline, cfg_mqtt_lwt_msg_online;
uint8_t cfg_mqtt_lwt_qos;
bool cfg_mqtt_lwt_retain;
String cfg_http_url;
String cfg_aes_key;                  // 16 chars, must match sensor node
uint16_t cfg_magic;
uint8_t cfg_loraProfile;             // 1–4
String gwID;                         // Derived from ESP32 eFuse MAC

// Runtime state
unsigned long blTimer = 0;
bool blState = true;
SecurePacket lastData;
int lastRssi = -999;
bool hasData = false;
char lastRxTime[10] = "--:--";
unsigned long lastMqttReconnect = 0;
unsigned long mqttRetryDelay = MQTT_RETRY_INITIAL_DELAY;
uint8_t mqttRetryCount = 0;
unsigned long lastWifiCheck = 0;
bool otaInProgress = false;
bool apMode = false;                 // AP mode: captive portal, no LoRa/MQTT

// Health monitoring
bool ntpSynced = false;
bool mdnsStarted = false;            // Lazy mDNS init (may start after delayed WiFi connect)
unsigned long bootTime = 0;
uint32_t loraPacketCount = 0;
unsigned long lastHeartbeat = 0;

// Button state
unsigned long btnPressStart = 0;
bool btnLongPressHandled = false;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void loadSettings();
void applyLoRaProfile(uint8_t profile);
void resetWiFiSettings();
void setupWiFi();
void setupWebServer();
bool checkAuth();
bool connectMQTT();
void sendToCloud(String json);
void updateDisplay();
void handleLoRa();
void triggerBacklight();
void drawStatusBar();
void drawStaticLabels();
float calculateAltitude(float pressure);
String getISO8601Timestamp();

// ============================================================================
// SETUP — System Initialization
// ============================================================================
// Boot order: GPIO -> NVS -> TFT -> LoRa -> WiFi/NTP -> Web Server -> WDT
// The watchdog is fed at blocking points (WiFi, NTP) to prevent spurious resets.
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=================================");
  Serial.print("AetherLogic Gateway v");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("=================================\n");

  // Generate unique gateway ID from last 3 bytes of eFuse MAC
  uint64_t chipid = ESP.getEfuseMac();
  char idBuffer[13];
  sprintf(idBuffer, "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
  String fullMac = String(idBuffer);
  gwID = fullMac.substring(6);
  Serial.println("Gateway ID: " + gwID);

  // Backlight ON first for immediate visual feedback
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  blState = true;

  pinMode(BTN_PIN, INPUT_PULLUP);

  loadSettings();

  // TFT Init — SPI Mode 3, write-only (no MISO), rotation for enclosure orientation
  tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320, SPI_MODE3);
  tft.setRotation(2);
  tft.invertDisplay(true);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.print("System Boot v");
  tft.println(FIRMWARE_VERSION);
  tft.setCursor(10, 130);
  tft.println("GW ID: " + gwID);

  // LoRa Init — FSPI bus, 1 MHz SPI clock, manual hardware reset
  Serial.println("Initializing LoRa FSPI...");

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  LoRa.setSPI(loraSPI);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  LoRa.setSPIFrequency(1000000);

  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(10);
  digitalWrite(LORA_RST, HIGH);
  delay(100);

  if (!LoRa.begin(868E6)) {
    Serial.println("LoRa Failed!");
    loraInitOK = false;
    tft.setTextColor(ST77XX_RED);
    tft.println("LoRa ERROR");
  } else {
    applyLoRaProfile(cfg_loraProfile);

    loraInitOK = true;
    Serial.printf("LoRa OK - Profile %d\n", cfg_loraProfile);
    tft.setTextColor(ST77XX_GREEN);
    tft.println("LoRa READY");
  }

  // WiFi, TLS, NTP — setInsecure() skips cert validation (self-signed broker cert)
  espClient.setInsecure();
  mqtt.setBufferSize(768);      // Larger than default 256 for ISO 8601 timestamps in JSON

  setupWiFi();
  esp_task_wdt_reset();

  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
  esp_task_wdt_reset();

  if(WiFi.status() == WL_CONNECTED) {
    if(MDNS.begin(HOSTNAME)) {
      Serial.println("mDNS responder: http://" HOSTNAME ".local");
      mdnsStarted = true;
    }
  }

  if (cfg_mqtt_server != "") {
    mqtt.setServer(cfg_mqtt_server.c_str(), cfg_mqtt_port);
  }

  setupWebServer();
  server.begin();

  delay(2000);
  esp_task_wdt_reset();

  tft.fillScreen(ST77XX_BLACK);
  drawStaticLabels();
  drawStatusBar();

  blTimer = millis();
  bootTime = millis();
  Serial.println("Boot complete. Backlight timer started.");

  // Watchdog Timer — 10s timeout, triggers panic (hardware restart) on hang.
  // ESP32 Core 3.x syntax (not the legacy esp_task_wdt_init(timeout) API).
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 10000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog enabled - 10s timeout");
}


// ============================================================================
// MAIN LOOP — Cooperative Multitasking
// ============================================================================
// Three mutually exclusive operating modes:
//   1. OTA Mode:    Only HTTP server runs (watchdog fed by upload handler)
//   2. AP Mode:     Captive portal only (DNS + HTTP + button). No LoRa/MQTT.
//   3. Normal Mode: Full operation — LoRa, MQTT, display, health monitoring
void loop() {
  unsigned long loopStart = millis();

  // Feed watchdog (during OTA, it's fed in the upload handler instead)
  if (!otaInProgress) {
    esp_task_wdt_reset();
  }

  unsigned long now = millis();

  // 12-hour scheduled reboot — works in ALL modes (AP, normal, OTA, reconnecting)
  if (now - bootTime > REBOOT_INTERVAL) {
    Serial.println("=== 12-HOUR SCHEDULED REBOOT ===");
    delay(100);  // flush serial
    ESP.restart();
  }

  // OTA Mode — suspend everything else during firmware upload
  if (otaInProgress) {
    server.handleClient();
    delay(10);
    return;
  }

  // AP Mode — captive portal only, no LoRa/MQTT
  if (apMode) {
    dnsServer.processNextRequest();
    server.handleClient();

    int btnState = digitalRead(BTN_PIN);
    if (btnState == LOW) {
      if (btnPressStart == 0) {
        btnPressStart = millis();
        btnLongPressHandled = false;
      }
      if (!btnLongPressHandled && (millis() - btnPressStart > LONG_PRESS_MS)) {
        resetWiFiSettings();
        btnLongPressHandled = true;
      }
    } else {
      btnPressStart = 0;
    }

    delay(10);
    return;
  }

  // =========================================================================
  // NORMAL MODE OPERATIONS (WiFi STA connected or attempting reconnection)
  // =========================================================================

  // WiFi auto-reconnect every 30 s (full disconnect cycle clears stale state)
  if (cfg_ssid != "" && WiFi.status() != WL_CONNECTED) {
      if (now - lastWifiCheck > 30000) {
          lastWifiCheck = now;
          Serial.println("WiFi Lost. Attempting Reconnect...");
          WiFi.disconnect();
          WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
      }
  }

  // Detect fresh WiFi connection — initialize NTP + mDNS on first connect/reconnect
  static bool wasConnected = false;
  if (WiFi.status() == WL_CONNECTED && !wasConnected) {
    Serial.println("WiFi connected! Initializing network services...");
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    if (!mdnsStarted) {
      if (MDNS.begin(HOSTNAME)) {
        Serial.println("mDNS started: http://" HOSTNAME ".local");
        mdnsStarted = true;
      }
    }
    wasConnected = true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    wasConnected = false;
  }

  if (WiFi.status() == WL_CONNECTED) {
      // MQTT reconnection with exponential backoff (2s -> 4s -> ... -> 60s max)
      if (cfg_mqtt_server != "" && !mqtt.connected()) {
          if (now - lastMqttReconnect > mqttRetryDelay) {
              lastMqttReconnect = now;

              bool connected = connectMQTT();

              if (connected) {
                  mqttRetryDelay = MQTT_RETRY_INITIAL_DELAY;
                  mqttRetryCount = 0;
                  Serial.println("MQTT backoff reset");
              } else {
                  mqttRetryCount++;
                  mqttRetryDelay = min(mqttRetryDelay * 2, MQTT_RETRY_MAX_DELAY);
                  Serial.printf("MQTT retry #%d - next attempt in %lus\n",
                               mqttRetryCount, mqttRetryDelay/1000);
              }
          }
      }

      if (cfg_mqtt_server != "" && mqtt.connected()) {
          mqtt.loop();

          unsigned long flushStart = millis();
          if (!msgBuffer.empty()) {
              esp_task_wdt_reset();  // Prevent watchdog during slow MQTT publish
              String bufferedJson = msgBuffer.front();
              Serial.print("Flushing Buffer... ");
              if (mqtt.publish(cfg_mqtt_topic.c_str(), bufferedJson.c_str(), cfg_mqtt_qos == 1)) {
                  Serial.println("Sent!");
                  msgBuffer.erase(msgBuffer.begin());
                  delay(50);
              } else {
                  Serial.println("Failed.");
              }

              if (millis() - flushStart > 1000) {
                Serial.println("WARNING: MQTT publish timeout!");
              }
          }
      }
  } else {
      dnsServer.processNextRequest();
  }

  server.handleClient();

  // LoRa reception (>2 s execution warns of potential SPI hang)
  unsigned long loraStart = millis();
  handleLoRa();
  unsigned long loraTime = millis() - loraStart;

  if (loraTime > 2000) {
    Serial.printf("WARNING: handleLoRa() took %lu ms!\n", loraTime);
  }

  // LoRa inactivity watchdog — hardware reset after 15 min silence
  // (EMI-induced lockups observed in metallic industrial environments)
  static unsigned long lastLoRaActivity = 0;
  static unsigned long loraCheckTimer = 0;

  if (hasData) lastLoRaActivity = millis();

  if (millis() - loraCheckTimer > 300000) {
    loraCheckTimer = millis();

    if (millis() - lastLoRaActivity > 900000 && loraInitOK) {
      Serial.println("LoRa TIMEOUT! Resetting module...");

      digitalWrite(LORA_RST, LOW);
      delay(100);
      digitalWrite(LORA_RST, HIGH);
      delay(500);

      if (LoRa.begin(868E6)) {
        applyLoRaProfile(cfg_loraProfile);
        Serial.println("LoRa recovered!");
        lastLoRaActivity = millis();
      } else {
        Serial.println("LoRa recovery FAILED!");
        loraInitOK = false;
      }
    }
  }

  // Button: short press = backlight, long press (>3 s) = factory reset
  int btnState = digitalRead(BTN_PIN);
  if (btnState == LOW) {
    if (btnPressStart == 0) {
      btnPressStart = millis();
      btnLongPressHandled = false;

      tft.fillRect(0, 0, 240, 20, ST77XX_YELLOW);
      tft.setTextColor(ST77XX_BLACK);
      tft.setTextSize(1);
      tft.setCursor(5, 5);
      tft.print("Button pressed...");
    }

    unsigned long pressDuration = millis() - btnPressStart;
    if (pressDuration < LONG_PRESS_MS) {
      int progress = map(pressDuration, 0, LONG_PRESS_MS, 0, 230);
      tft.fillRect(5, 15, progress, 3, ST77XX_RED);
    }

    if (!btnLongPressHandled && (millis() - btnPressStart > LONG_PRESS_MS)) {
      resetWiFiSettings();
      btnLongPressHandled = true;
    }
  } else {
    if (btnPressStart > 0) {
      if (!btnLongPressHandled) {
        triggerBacklight();

        tft.fillRect(0, 0, 240, 20, ST77XX_GREEN);
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(1);
        tft.setCursor(5, 5);
        tft.print("Backlight ON!");
        delay(500);
        drawStatusBar();
      }
      btnPressStart = 0;
    }
  }

  // Backlight auto-off after BL_TIMEOUT
  if (blState && (millis() - blTimer > BL_TIMEOUT)) {
    digitalWrite(TFT_BL, LOW);
    blState = false;
  }

  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate > 1000 || hasData) {
    drawStatusBar();
    lastStatusUpdate = millis();
    hasData = false;
  }

  unsigned long loopTime = millis() - loopStart;
  if (loopTime > 5000) {
    Serial.printf("CRITICAL: Loop took %lu ms!\n", loopTime);
  }

  // MQTT heartbeat — retained health report every 5 min
  if (WiFi.status() == WL_CONNECTED && mqtt.connected() && (millis() - lastHeartbeat > HEARTBEAT_INTERVAL)) {
    lastHeartbeat = millis();

    unsigned long uptime = (millis() - bootTime) / 1000;
    uint32_t freeHeap = ESP.getFreeHeap();
    int wifiRssi = WiFi.RSSI();

    char heartbeat[450];
    sprintf(heartbeat, "{\"type\":\"heartbeat\",\"gw\":\"%s\",\"fw\":\"%s\",\"ip\":\"%s\",\"uptime\":%lu,\"heap\":%u,\"wifi_rssi\":%d,\"lora_packets\":%u,\"buffer\":%d,\"ntp_synced\":%s,\"lora_ok\":%s}",
            gwID.c_str(),
            FIRMWARE_VERSION,
            WiFi.localIP().toString().c_str(),
            uptime,
            freeHeap,
            wifiRssi,
            loraPacketCount,
            msgBuffer.size(),
            ntpSynced ? "true" : "false",
            loraInitOK ? "true" : "false");

    String healthTopic = "aetherlogic/gateway/" + gwID + "/health";
    if (mqtt.publish(healthTopic.c_str(), heartbeat, true)) {
      Serial.println("Heartbeat sent");
    }
  }

  // NTP validation — year >= 2024 confirms sync (ESP32 defaults to 1970)
  static unsigned long lastNtpCheck = 0;
  if (millis() - lastNtpCheck > 30000) {
    lastNtpCheck = millis();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo) && (timeinfo.tm_year + 1900 >= 2024)) {
      if (!ntpSynced) {
        Serial.println("NTP synced successfully!");
        ntpSynced = true;
      }
    } else {
      if (ntpSynced) {
        Serial.println("WARNING: NTP sync lost!");
        ntpSynced = false;
      }
    }
  }

  // Heap monitor — emergency cleanup below 10 KB to prevent crash
  static unsigned long lastMemCheck = 0;
  if (millis() - lastMemCheck > 60000) {
    lastMemCheck = millis();
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("Health: Free Heap=%d bytes, Buffer=%d msgs, LoRa=%s\n",
                  freeHeap, msgBuffer.size(), loraInitOK ? "OK" : "FAIL");

    if (freeHeap < 10000) {
      Serial.println("CRITICAL LOW MEMORY! Emergency cleanup...");
      msgBuffer.clear();

      if (lastMessages.size() > 2) {
        lastMessages.erase(lastMessages.begin() + 2, lastMessages.end());
      }

      Serial.printf("Cleanup done. Free Heap now: %d bytes\n", ESP.getFreeHeap());
    }

    if (msgBuffer.size() > 40) {
      Serial.printf("WARNING: Buffer almost full (%d/%d)\n", msgBuffer.size(), MAX_BUFFER_SIZE);
    }
  }
}

// ============================================================================
// CORE LOGIC — LoRa Radio, Packet Processing, Cloud Forwarding
// ============================================================================

// ----------------------------------------------------------------------------
// applyLoRaProfile — Configure LoRa Radio Parameters
// ----------------------------------------------------------------------------
// Configures the SX1276 transceiver with one of four predefined radio profiles
// that trade off data rate, range, and power consumption. The selected profile
// must match the sensor node configuration for successful communication.
//
// LoRa Modulation Parameters (Semtech SX1276 datasheet, Table 23):
//
//   Profile  SF   BW [kHz]  CR     TX [dBm]  Approx. Range   Data Rate
//   -------  ---  --------  -----  --------  --------------  ----------
//   1 ECO    7    125       4/5    14        ~2 km           5.47 kbps
//   2 BAL    9    125       4/5    17        ~4 km           1.76 kbps
//   3 LONG   10   125       4/6    20        ~8 km           0.98 kbps
//   4 EXT    12   125       4/8    20        ~15 km          0.29 kbps
//
// After configuration, the sync word is set to 0x12 (private network, distinct
// from the LoRaWAN sync word 0x34) and CRC is enabled for error detection.
// The module is then placed in continuous receive mode.
//
// @param profile  Radio profile index (1–4). Invalid values default to 3 (LONG).
void applyLoRaProfile(uint8_t profile) {
  Serial.printf("Applying LoRa Profile %d...\n", profile);

  LoRa.idle();

  switch(profile) {
    case 1: // ECO — Short range, low power, highest throughput
      LoRa.setSpreadingFactor(7);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(5);
      LoRa.setTxPower(14);
      Serial.println("LoRa: ECO (SF7, 14dBm, ~2km)");
      break;

    case 2: // BALANCED — Medium range, moderate power
      LoRa.setSpreadingFactor(9);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(5);
      LoRa.setTxPower(17);
      Serial.println("LoRa: BALANCED (SF9, 17dBm, ~4km)");
      break;

    case 3: // LONG (DEFAULT) — Optimized for industrial environments with metallic obstructions
      LoRa.setSpreadingFactor(10);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(6);
      LoRa.setTxPower(20);
      Serial.println("LoRa: LONG (SF10, 20dBm, ~8km)");
      break;

    case 4: // EXTREME — Maximum range, lowest data rate, highest link budget
      LoRa.setSpreadingFactor(12);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(8);
      LoRa.setTxPower(20);
      Serial.println("LoRa: EXTREME (SF12, 20dBm, ~15km)");
      break;

    default:
      Serial.println("Invalid profile! Using LONG (3)");
      profile = 3;
      applyLoRaProfile(3);
      return;
  }

  LoRa.setSyncWord(0x12);  // Private network sync word (0x12 != LoRaWAN 0x34)
  LoRa.enableCrc();         // Enable CRC for on-air error detection
  LoRa.receive();           // Enter continuous receive mode
}

// ----------------------------------------------------------------------------
// handleLoRa — Receive, Decrypt, Validate, and Forward LoRa Packets
// ----------------------------------------------------------------------------
// Polls for incoming 48-byte packets, decrypts via AES-128-ECB, validates the
// magic number, then publishes to MQTT (with offline buffering fallback) and
// optionally forwards via HTTP POST. Invalid packets are silently discarded.
void handleLoRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 48) {
    triggerBacklight();

    // AES-128-ECB decryption (3 blocks x 16 bytes)
    uint8_t encrypted[48];
    uint8_t decrypted[48];
    for (int i=0; i<48; i++) encrypted[i] = (uint8_t)LoRa.read();

    uint8_t keyBytes[16];
    cfg_aes_key.getBytes(keyBytes, 17);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, keyBytes, 128);
    for(int i=0; i<3; i++) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, encrypted + (i*16), decrypted + (i*16));
    }
    mbedtls_aes_free(&aes);

    SecurePacket* pkt = (SecurePacket*)decrypted;

    if (pkt->magic == cfg_magic) {
      loraPacketCount++;
      lastRssi = LoRa.packetRssi();
      memcpy(&lastData, pkt, sizeof(SecurePacket));

      struct tm timeinfo;
      if(getLocalTime(&timeinfo)){
         sprintf(lastRxTime, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      } else {
         sprintf(lastRxTime, "N/A");
      }

      hasData = true;
      updateDisplay();

      MessageLog msg;
      msg.deviceName = "Device " + String(pkt->deviceId);
      msg.deviceId = pkt->deviceId;
      msg.packetId = pkt->packetId;
      msg.rssi = lastRssi;
      msg.timestamp = String(lastRxTime);
      msg.temp = pkt->temp;
      msg.hum = pkt->hum;
      msg.co2 = pkt->co2;

      lastMessages.insert(lastMessages.begin(), msg);
      if (lastMessages.size() > 5) {
        lastMessages.pop_back();
      }

      String timestamp = getISO8601Timestamp();

      char json[600];
      sprintf(json, "{\"timestamp\":\"%s\",\"gw\":\"%s\",\"id\":%d,\"pkt\":%d,\"v\":%.2f,\"db\":%.1f,\"temp\":%.1f,\"hum\":%.1f,\"press\":%.1f,\"iaq\":%.0f,\"tvoc\":%.2f,\"co2\":%d,\"pm1\":%d,\"pm25\":%d,\"pm10\":%d,\"rssi\":%d}",
              timestamp.c_str(),
              gwID.c_str(),
              pkt->deviceId, pkt->packetId, pkt->voltage, pkt->noise_db,
              pkt->temp, pkt->hum, pkt->pressure,
              pkt->iaq, pkt->tvoc, pkt->co2,
              pkt->pm1, pkt->pm25, pkt->pm10,
              lastRssi);

      // Publish to MQTT or buffer offline
      bool sent = false;

      if (WiFi.status() == WL_CONNECTED && cfg_mqtt_server != "" && mqtt.connected()) {
        if (mqtt.publish(cfg_mqtt_topic.c_str(), json, cfg_mqtt_qos == 1)) {
            sent = true;
            Serial.printf("MQTT Sent (QoS %d)\n", cfg_mqtt_qos);
        }
      }

      if (!sent) {
         if (msgBuffer.size() >= MAX_BUFFER_SIZE) {
             msgBuffer.erase(msgBuffer.begin());
             Serial.println("Buffer Full: Dropped Oldest");
         }
         msgBuffer.push_back(String(json));
         Serial.printf("Offline! Buffered Packet #%d. Queue Size: %d\n", pkt->packetId, msgBuffer.size());
      }

      if (cfg_http_url != "" && WiFi.status() == WL_CONNECTED) {
        sendToCloud(String(json));
      }

    } else {
      Serial.printf("Invalid Magic: %04X\n", pkt->magic);
    }
  }
}

// Fire-and-forget HTTP POST to optional secondary cloud endpoint.
void sendToCloud(String jsonPayload) {
  HTTPClient http;
  http.begin(cfg_http_url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonPayload);
  http.end();
}

// Barometric altitude estimation (ISO 2533:1975): h = 44330 * (1 - (P/P0)^0.1903)
float calculateAltitude(float pressure) {
  if (pressure == 0) return 0;
  return 44330.0 * (1.0 - pow(pressure / 1013.25, 0.1903));
}

// Returns ISO 8601 timestamp with timezone offset (e.g., "2025-01-15T14:30:45+02:00").
// Falls back to "1970-01-01T00:00:00Z" if NTP has not synced.
String getISO8601Timestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo) || (timeinfo.tm_year + 1900 < 2024)) {
    return "1970-01-01T00:00:00Z";
  }

  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);

  int tzOffset = GMT_OFFSET_SEC + (timeinfo.tm_isdst ? DST_OFFSET_SEC : 0);
  int tzHours = tzOffset / 3600;
  int tzMins = abs(tzOffset % 3600) / 60;

  char tz[10];
  sprintf(tz, "%+03d:%02d", tzHours, tzMins);

  return String(timestamp) + String(tz);
}

// Clears WiFi and web password from NVS, then reboots into AP/captive portal mode.
void resetWiFiSettings() {
  triggerBacklight();
  tft.fillScreen(ST77XX_RED);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 120);
  tft.println("WiFi RESET!");
  tft.setCursor(20, 150);
  tft.println("Password RESET!");

  prefs.begin("gateway_cfg", false);
  prefs.putString("ssid", "");
  prefs.putString("pass", "");
  prefs.putString("web_pw", DEFAULT_WEB_PASSWORD);
  prefs.end();

  delay(2000);
  ESP.restart();
}

void triggerBacklight() {
  digitalWrite(TFT_BL, HIGH);
  blState = true;
  blTimer = millis();
}

// ============================================================================
// USER INTERFACE — TFT Display Rendering (240x320, ST7789)
// ============================================================================
// Layout: Status bar (0–34) | Node info (35–99) | Climate (100–159) |
//         Air quality (160–229) | Particulates (230–320)

// Draws static section headers and dividers. Called once at boot.
void drawStaticLabels() {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GRAY);
  
  tft.drawFastHLine(0, 42, 240, ST77XX_WHITE);

  int y = 50;
  tft.setCursor(5, y); tft.print("NODE INFO");
  tft.drawFastHLine(0, y+10, 240, ST77XX_DARKGREY);
  
  y = 100;
  tft.setCursor(5, y); tft.print("CLIMATE");
  tft.drawFastHLine(0, y+10, 240, ST77XX_DARKGREY);

  y = 160;
  tft.setCursor(5, y); tft.print("AIR QUALITY");
  tft.drawFastHLine(0, y+10, 240, ST77XX_DARKGREY);

  y = 230;
  tft.setCursor(5, y); tft.print("PARTICULATES");
  tft.drawFastHLine(0, y+10, 240, ST77XX_DARKGREY);
}

// Status bar: WiFi signal (left), MQTT/buffer status (center), clock (right).
void drawStatusBar() {
  tft.fillRect(0, 0, 240, 30, ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);

  int startX = 5;
  int startY = 22;
  int barW = 4;
  int gap = 2;

  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    int bars = 0;
    if(rssi > -90) bars = 1;   // Weak signal
    if(rssi > -80) bars = 2;   // Fair signal
    if(rssi > -70) bars = 3;   // Good signal
    if(rssi > -60) bars = 4;   // Excellent signal

    for(int i=0; i<4; i++) {
        int h = 4 + (i*4);     // Progressive bar heights: 4, 8, 12, 16 px
        uint16_t color = (i < bars) ? ST77XX_WHITE : ST77XX_DARKGREY;
        tft.fillRect(startX + (i*(barW+gap)), startY - h, barW, h, color);
    }
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.setCursor(startX, 8);
    tft.print("X");
  }

  if (cfg_mqtt_server != "" && mqtt.connected()) {
      tft.setCursor(90, 8);
      tft.setTextColor(ST77XX_GREEN);
      tft.setTextSize(2);

      if (!msgBuffer.empty()) {
         tft.setTextColor(ST77XX_YELLOW);
         tft.printf("BUF:%d", msgBuffer.size());
      } else {
         tft.print("MQTT");
      }
  } else {
      if (!msgBuffer.empty()) {
         tft.setCursor(90, 8);
         tft.setTextColor(ST77XX_ORANGE);
         tft.setTextSize(2);
         tft.printf("OFF:%d", msgBuffer.size());
      }
  }

  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(170, 8);
    tft.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  }

  // IP address strip (below main status bar)
  tft.fillRect(0, 30, 240, 12, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 32);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(ST77XX_GRAY);
    tft.print("IP: ");
    tft.print(WiFi.localIP().toString());
  } else if (apMode) {
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("AP: ");
    tft.print(WiFi.softAPIP().toString());
  } else {
    tft.setTextColor(ST77XX_ORANGE);
    tft.print("WiFi: reconnecting...");
  }
}

// Updates dynamic sensor values on TFT. Color thresholds: IAQ < 100 (green),
// CO2 < 1000 ppm, PM2.5 < 12 ug/m3 (EPA "Good" AQI), RSSI > -100 dBm.
void updateDisplay() {
  tft.fillRect(0, 62, 240, 30, ST77XX_BLACK);
  tft.setCursor(10, 65);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.printf("ID:%d #%d", lastData.deviceId, lastData.packetId);

  tft.setCursor(150, 65);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.printf("%.2fV @%s", lastData.voltage, lastRxTime);

  tft.fillRect(0, 112, 240, 40, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 115);
  tft.printf("%.1fC  %.0f%%", lastData.temp, lastData.hum);

  tft.setTextSize(1);
  tft.setCursor(160, 120);
  tft.setTextColor(ST77XX_GRAY);
  tft.printf("Alt:%.0fm", calculateAltitude(lastData.pressure));

  tft.fillRect(0, 172, 240, 50, ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setCursor(10, 175);
  tft.setTextColor((lastData.iaq < 100) ? ST77XX_GREEN : ST77XX_RED);
  tft.printf("VOC:%.1f", lastData.tvoc);

  tft.setCursor(130, 175);
  tft.setTextColor((lastData.co2 < 1000) ? ST77XX_GREEN : ST77XX_RED);
  tft.printf("CO2:%d", lastData.co2);

  tft.setTextSize(1);
  tft.setCursor(10, 195);
  tft.setTextColor(ST77XX_MAGENTA);
  tft.printf("Noise: %.1f dB  (IAQ: %.0f)", lastData.noise_db, lastData.iaq);

  tft.fillRect(0, 242, 240, 70, ST77XX_BLACK);
  tft.setTextSize(2);

  tft.setCursor(10, 245);
  tft.setTextColor((lastData.pm25 < 12) ? ST77XX_GREEN : ST77XX_RED);
  tft.printf("PM2.5: %d", lastData.pm25);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 270);
  tft.printf("PM1.0: %d   PM10: %d", lastData.pm1, lastData.pm10);

  tft.setCursor(150, 270);
  tft.setTextColor((lastRssi > -100) ? ST77XX_GREEN : ST77XX_RED);
  tft.printf("Sig: %ddBm", lastRssi);
}

// ============================================================================
// PERSISTENT CONFIGURATION — NVS Load and Validation
// ============================================================================

// Reads all configuration from NVS ("gateway_cfg" namespace) with compile-time
// defaults for first boot. Boundary validation applied after loading.
void loadSettings() {
  prefs.begin("gateway_cfg", true);     // Open NVS in read-only mode
  cfg_ssid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  cfg_pass = prefs.getString("pass", DEFAULT_WIFI_PASS);
  cfg_web_password = prefs.getString("web_pw", DEFAULT_WEB_PASSWORD);
  cfg_mqtt_server = prefs.getString("mq_srv", DEFAULT_MQTT_SERVER);
  cfg_mqtt_port = prefs.getInt("mq_prt", DEFAULT_MQTT_PORT);
  cfg_mqtt_user = prefs.getString("mq_usr", DEFAULT_MQTT_USER);
  cfg_mqtt_pass = prefs.getString("mq_pw", DEFAULT_MQTT_PASS);
  cfg_mqtt_topic = prefs.getString("mq_top", DEFAULT_MQTT_TOPIC);
  cfg_mqtt_qos = prefs.getUChar("mq_qos", DEFAULT_MQTT_QOS);
  cfg_mqtt_keepalive = prefs.getUShort("mq_kalive", DEFAULT_MQTT_KEEPALIVE);
  cfg_mqtt_lwt_topic = prefs.getString("lwt_top", DEFAULT_MQTT_LWT_TOPIC);
  cfg_mqtt_lwt_msg_offline = prefs.getString("lwt_off", DEFAULT_MQTT_LWT_MSG_OFFLINE);
  cfg_mqtt_lwt_msg_online = prefs.getString("lwt_on", DEFAULT_MQTT_LWT_MSG_ONLINE);
  cfg_mqtt_lwt_qos = prefs.getUChar("lwt_qos", DEFAULT_MQTT_LWT_QOS);
  cfg_mqtt_lwt_retain = prefs.getBool("lwt_ret", DEFAULT_MQTT_LWT_RETAIN);
  cfg_http_url = prefs.getString("http_url", DEFAULT_HTTP_URL);
  cfg_aes_key = prefs.getString("aes", DEFAULT_AES_KEY);
  cfg_magic = (uint16_t)prefs.getUInt("magic", DEFAULT_MAGIC);
  cfg_loraProfile = prefs.getUChar("lora_prf", DEFAULT_LORA_PROFILE);
  prefs.end();

  // Boundary validation
  if(cfg_aes_key.length() != 16) cfg_aes_key = DEFAULT_AES_KEY;
  if(cfg_loraProfile < 1 || cfg_loraProfile > 4) cfg_loraProfile = 3;
  if(cfg_mqtt_qos > 2) cfg_mqtt_qos = DEFAULT_MQTT_QOS;
  if(cfg_mqtt_lwt_qos > 2) cfg_mqtt_lwt_qos = DEFAULT_MQTT_LWT_QOS;
  if(cfg_mqtt_keepalive < 10 || cfg_mqtt_keepalive > 300) cfg_mqtt_keepalive = DEFAULT_MQTT_KEEPALIVE;
}

// ============================================================================
// NETWORK INITIALIZATION
// ============================================================================

// Connects to WiFi in STA mode. If no SSID is configured, starts AP for setup.
// If SSID exists but connection fails, stays in normal mode (LoRa + reconnect
// loop) instead of falling back to AP — prevents LoRa reception lockout.
void setupWiFi() {
  if (cfg_ssid == "") {
    // No stored network → AP mode forever, waiting for configuration
    Serial.println("No WiFi configured. Starting AP for setup.");
    WiFi.softAP("S3-Gateway-Setup", "admin123");
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    apMode = true;
    Serial.printf("AP Mode: Connect to 'S3-Gateway-Setup' at %s\n",
                  WiFi.softAPIP().toString().c_str());
  } else {
    // Stored network exists → attempt connection (~20s timeout)
    WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40) {
      esp_task_wdt_reset();
      delay(500); Serial.print("."); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi Connected!");
      Serial.println(WiFi.localIP());
    } else {
      // Do NOT fall back to AP mode — main loop will keep retrying
      // while LoRa reception and buffering remain active
      Serial.println("\nWiFi not connected. Will retry in main loop.");
      Serial.println("LoRa reception and buffering active.");
    }
    apMode = false;
  }
}

// HTTP Basic Auth guard — returns false and sends 401 if unauthenticated.
bool checkAuth() {
  if (!server.authenticate("admin", cfg_web_password.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// ============================================================================
// WEB SERVER — Configuration Portal, Status Dashboard, and OTA Updates
// ============================================================================
// All routes require HTTP Basic Auth. HTML built via string concatenation
// with raw string literals for CSS/boilerplate.
void setupWebServer() {

  // GET / — Main configuration page
  server.on("/", HTTP_GET, []() {
    if (!checkAuth()) return;

    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>S3 Gateway Config</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f2f4f8; margin: 0; padding: 20px; color: #333; }
    .container { max-width: 450px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0,0,0,0.08); }
    h2 { text-align: center; color: #2c3e50; margin-bottom: 5px; margin-top: 0; }
    p.sub { text-align: center; color: #7f8c8d; font-size: 0.9em; margin-bottom: 25px; }
    h3 { color: #34495e; border-bottom: 2px solid #ecf0f1; padding-bottom: 8px; margin-top: 25px; font-size: 1.1em; }
    label { font-weight: 600; font-size: 0.85em; color: #555; display: block; margin-bottom: 4px; margin-top: 12px; }
    input[type=text], input[type=password], input[type=number], select { width: 100%; padding: 10px 12px; margin: 0 0 10px 0; display: inline-block; border: 1px solid #ccc; border-radius: 6px; box-sizing: border-box; font-size: 14px; transition: border 0.3s; }
    input[type=text]:focus, input[type=password]:focus, input[type=number]:focus, select:focus { border-color: #3498db; outline: none; }
    input[type=submit] { width: 100%; background-color: #27ae60; color: white; padding: 12px 20px; margin: 20px 0 0 0; border: none; border-radius: 6px; cursor: pointer; font-size: 16px; font-weight: bold; transition: background 0.3s; }
    input[type=submit]:hover { background-color: #219150; }
    .ota-section { background: #f8f9fa; padding: 15px; border-radius: 8px; border: 1px dashed #bdc3c7; text-align: center; margin-top: 20px; }
    .ota-btn { background-color: #3498db; margin-top: 10px; }
    .ota-btn:hover { background-color: #2980b9; }
    input[type=file] { margin-top: 10px; font-size: 0.9em; }
    .reset-btn { display: inline-block; background: #e74c3c; color: white; padding: 10px 20px; border-radius: 6px; text-decoration: none; font-weight: bold; margin-top: 10px; }
    .reset-btn:hover { background: #c0392b; }
  </style>
</head>
<body>
  <div class="container">
    <h2>Gateway Setup</h2>
    <p class="sub">LoRa to MQTT Bridge Configuration</p>
    <form method='POST' action='/save'>
)rawliteral";

    html += "<h3>Wi-Fi Settings</h3>";
    html += "<label>SSID</label><input type='text' name='ssid' value='" + cfg_ssid + "' placeholder='Network Name'>";
    html += "<label>Password</label><input type='password' name='pass' value='" + cfg_pass + "' placeholder='Network Password'>";

    html += "<h3>Web Interface Security</h3>";
    html += "<label>Web Password</label><input type='password' name='web_pw' value='" + cfg_web_password + "' placeholder='Web interface password'>";
    html += "<p style='font-size:0.85em; color:#7f8c8d; margin-top:-5px;'>Change the password for accessing this configuration page. Default: @dmin12#4</p>";

    html += "<h3>Cloud API (Optional)</h3>";
    html += "<label>HTTP URL</label><input type='text' name='http_url' value='" + cfg_http_url + "' placeholder='http://your-site.com/api'>";
    
    html += "<h3>MQTT Broker</h3>";
    html += "<label>Server Address</label><input type='text' name='mq_srv' value='" + cfg_mqtt_server + "' placeholder='broker.emqx.io'>";
    html += "<label>Port (8883 for SSL)</label><input type='number' name='mq_prt' value='" + String(cfg_mqtt_port) + "'>";
    html += "<label>Username</label><input type='text' name='mq_usr' value='" + cfg_mqtt_user + "'>";
    html += "<label>Password</label><input type='password' name='mq_pw' value='" + cfg_mqtt_pass + "'>";
    html += "<label>Topic</label><input type='text' name='mq_top' value='" + cfg_mqtt_topic + "'>";

    html += "<label>Quality of Service (QoS)</label>";
    html += "<select name='mq_qos'>";
    for(int i=0; i<=2; i++) {
      html += "<option value='" + String(i) + "'";
      if(i == cfg_mqtt_qos) html += " selected";
      html += ">QoS " + String(i);
      if(i == 0) html += " (At most once)";
      else if(i == 1) html += " (At least once)";
      else html += " (Exactly once)";
      html += "</option>";
    }
    html += "</select>";

    html += "<label>Keepalive Interval (seconds)</label>";
    html += "<input type='number' name='mq_kalive' value='" + String(cfg_mqtt_keepalive) + "' min='10' max='300' placeholder='30'>";
    html += "<small style='color:#7f8c8d; display:block; margin-top:-10px; margin-bottom:15px;'>How often to ping the broker (10-300 seconds)</small>";

    html += "<h3>Last Will and Testament (LWT)</h3>";
    html += "<label>LWT Topic</label><input type='text' name='lwt_top' value='" + cfg_mqtt_lwt_topic + "' placeholder='aetherlogic/gateway/status'>";
    html += "<label>LWT Message (Offline)</label><input type='text' name='lwt_off' value='" + cfg_mqtt_lwt_msg_offline + "' placeholder='offline'>";
    html += "<label>LWT Message (Online)</label><input type='text' name='lwt_on' value='" + cfg_mqtt_lwt_msg_online + "' placeholder='online'>";

    html += "<label>LWT QoS</label>";
    html += "<select name='lwt_qos'>";
    for(int i=0; i<=2; i++) {
      html += "<option value='" + String(i) + "'";
      if(i == cfg_mqtt_lwt_qos) html += " selected";
      html += ">QoS " + String(i) + "</option>";
    }
    html += "</select>";

    html += "<label style='display:flex; align-items:center; margin-top:15px;'>";
    html += "<input type='checkbox' name='lwt_ret' value='1'";
    if(cfg_mqtt_lwt_retain) html += " checked";
    html += " style='width:auto; margin-right:8px;'> Retain LWT Message</label>";
    
    html += "<h3>Security</h3>";
    html += "<label>AES Key (16 chars)</label><input type='text' name='aes' value='" + cfg_aes_key + "'>";
    html += "<label>Magic Number (Hex)</label><input type='text' name='magic' value='" + String(cfg_magic, HEX) + "'>";
    
    html += "<h3>LoRa Reception Profile</h3>";
    html += "<label>Profile (Must Match Nodes)</label>";
    html += "<select name='lora_prf'>";
    
    String profiles[] = {"ECO (SF7, ~2km)", "BALANCED (SF9, ~4km)", "LONG (SF10, ~8km)", "EXTREME (SF12, ~15km)"};
    for(int i=1; i<=4; i++) {
      html += "<option value='" + String(i) + "'";
      if(i == cfg_loraProfile) html += " selected";
      html += ">" + String(i) + ": " + profiles[i-1] + "</option>";
    }
    html += "</select>";
    
    html += "<input type='submit' value='SAVE & REBOOT'>";
    html += "</form>";
    
    html += "<div style='text-align:center; margin:20px 0;'>";
    html += "<a href='/status' style='color:#3498db; font-weight:600; text-decoration:none; font-size:16px;'>[Status] View Gateway Status &rarr;</a>";
    html += "</div>";
    
    html += "<div class='ota-section'>";
    html += "<h3>Firmware Update</h3>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update'>";
    html += "<input type='submit' value='Upload .bin' class='ota-btn'>";
    html += "</form>";
    html += "<br><a href='/reset' class='reset-btn' onclick=\"return confirm('Reboot Gateway?')\">Reboot Gateway</a>";
    html += "</div>";
    
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  });

  // GET /status — auto-refreshing status dashboard (5 s interval)
  server.on("/status", HTTP_GET, []() {
    if (!checkAuth()) return;

    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <meta http-equiv='refresh' content='5'>
  <title>Gateway Status</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #f2f4f8; margin: 0; padding: 20px; }
    .container { max-width: 600px; margin: 0 auto; background: #fff; padding: 25px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0,0,0,0.08); }
    h2 { text-align: center; color: #2c3e50; margin-top: 0; }
    .status-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 25px; }
    .status-item { background: #f8f9fa; padding: 15px; border-radius: 8px; border-left: 4px solid #3498db; }
    .status-item.ok { border-left-color: #27ae60; }
    .status-item.error { border-left-color: #e74c3c; }
    .status-label { font-size: 0.85em; color: #7f8c8d; margin-bottom: 5px; }
    .status-value { font-size: 1.3em; font-weight: bold; color: #2c3e50; }
    .messages-section { margin-top: 25px; }
    .message-card { background: #f8f9fa; padding: 12px; border-radius: 8px; margin-bottom: 10px; border-left: 3px solid #3498db; }
    .message-header { display: flex; justify-content: space-between; margin-bottom: 8px; }
    .device-name { font-weight: bold; color: #2c3e50; }
    .timestamp { color: #7f8c8d; font-size: 0.9em; }
    .message-data { font-size: 0.9em; color: #555; }
    .nav-link { display: block; text-align: center; margin-top: 20px; color: #3498db; text-decoration: none; font-weight: 600; }
    .nav-link:hover { text-decoration: underline; }
    .reset-btn { display: inline-block; background: #e74c3c; color: white; padding: 10px 20px; border-radius: 6px; text-decoration: none; font-weight: bold; margin-top: 15px; }
    .reset-btn:hover { background: #c0392b; }
  </style>
</head>
<body>
  <div class="container">
    <h2>Gateway Status</h2>
    <p style='text-align:center; color:#7f8c8d; margin-top:-10px;'>Firmware v)rawliteral";
    html += FIRMWARE_VERSION;
    html += R"rawliteral(</p>

    <div class="status-grid">
      <div class="status-item )rawliteral";
    
    html += loraInitOK ? "ok" : "error";
    html += R"rawliteral(">
        <div class="status-label">LoRa Module</div>
        <div class="status-value">)rawliteral";
    html += loraInitOK ? "✓ OK" : "✗ FAILED";
    html += R"rawliteral(</div>
      </div>
      
      <div class="status-item">
        <div class="status-label">LoRa Profile</div>
        <div class="status-value">)rawliteral";
    String profileNames[] = {"", "ECO-SF7", "BAL-SF9", "LONG-SF10", "EXT-SF12"};
    html += profileNames[cfg_loraProfile];
    html += R"rawliteral(</div>
      </div>
      
      <div class="status-item )rawliteral";
    
    bool wifiOK = WiFi.status() == WL_CONNECTED;
    html += wifiOK ? "ok" : "error";
    html += R"rawliteral(">
        <div class="status-label">WiFi Signal</div>
        <div class="status-value">)rawliteral";
    if (wifiOK) {
      int rssi = WiFi.RSSI();
      html += String(rssi) + " dBm";
    } else {
      html += "Disconnected";
    }
    html += R"rawliteral(</div>
      </div>
      
      <div class="status-item )rawliteral";
    
    bool mqttOK = mqtt.connected();
    html += mqttOK ? "ok" : "error";
    html += R"rawliteral(">
        <div class="status-label">MQTT Broker</div>
        <div class="status-value">)rawliteral";
    html += mqttOK ? "✓ Connected" : "✗ Disconnected";
    html += R"rawliteral(</div>
      </div>
      
      <div class="status-item">
        <div class="status-label">Messages Received</div>
        <div class="status-value">)rawliteral";
    html += String(lastMessages.size());
    html += R"rawliteral(</div>
      </div>
      
      <div class="status-item">
        <div class="status-label">Buffer Size</div>
        <div class="status-value">)rawliteral";
    html += String(msgBuffer.size());
    html += R"rawliteral(</div>
      </div>

      <div class="status-item">
        <div class="status-label">MQTT QoS</div>
        <div class="status-value">)rawliteral";
    html += String(cfg_mqtt_qos);
    html += R"rawliteral(</div>
      </div>

      <div class="status-item">
        <div class="status-label">LWT Status</div>
        <div class="status-value">)rawliteral";
    html += cfg_mqtt_lwt_topic != "" ? "Enabled" : "Disabled";
    html += R"rawliteral(</div>
      </div>

      <div class="status-item )rawliteral";
    html += ntpSynced ? "ok" : "error";
    html += R"rawliteral(">
        <div class="status-label">NTP Sync</div>
        <div class="status-value">)rawliteral";
    html += ntpSynced ? "✓ Synced" : "✗ Not Synced";
    html += R"rawliteral(</div>
      </div>

      <div class="status-item">
        <div class="status-label">Uptime</div>
        <div class="status-value">)rawliteral";
    unsigned long uptime = (millis() - bootTime) / 1000;
    unsigned long days = uptime / 86400;
    unsigned long hours = (uptime % 86400) / 3600;
    unsigned long mins = (uptime % 3600) / 60;
    if (days > 0) {
      html += String(days) + "d " + String(hours) + "h";
    } else if (hours > 0) {
      html += String(hours) + "h " + String(mins) + "m";
    } else {
      html += String(mins) + "m";
    }
    html += R"rawliteral(</div>
      </div>

      <div class="status-item">
        <div class="status-label">LoRa Packets</div>
        <div class="status-value">)rawliteral";
    html += String(loraPacketCount);
    html += R"rawliteral(</div>
      </div>
    </div>

    <div class="messages-section">
      <h3 style="color: #34495e; border-bottom: 2px solid #ecf0f1; padding-bottom: 8px;">Last 5 Messages</h3>
      )rawliteral";
    
    if (lastMessages.empty()) {
      html += "<p style='text-align:center; color:#7f8c8d;'>No messages received yet...</p>";
    } else {
      for (const auto& msg : lastMessages) {
        html += "<div class='message-card'>";
        html += "<div class='message-header'>";
        html += "<span class='device-name'>" + msg.deviceName + " (Pkt #" + String(msg.packetId) + ")</span>";
        html += "<span class='timestamp'>" + msg.timestamp + "</span>";
        html += "</div>";
        html += "<div class='message-data'>";
        html += "Temp: " + String(msg.temp, 1) + "&deg;C | ";
        html += "Hum: " + String(msg.hum, 0) + "% | ";
        html += "CO2: " + String(msg.co2) + " ppm | ";
        html += "RSSI: " + String(msg.rssi) + " dBm";
        html += "</div>";
        html += "</div>";
      }
    }
    
    html += R"rawliteral(
    </div>
    
    <div style="text-align:center;">
      <a href="/" class="nav-link">Back to Configuration</a>
      <a href="/reset" class="reset-btn" onclick="return confirm('Reboot Gateway?')">Reboot Gateway</a>
    </div>
  </div>
</body>
</html>
)rawliteral";
    
    server.send(200, "text/html", html);
  });

  // Catch-all redirect for captive portal
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  // POST /save — persist to NVS and reboot
  server.on("/save", HTTP_POST, []() {
    if (!checkAuth()) return;

    prefs.begin("gateway_cfg", false);
    if(server.hasArg("ssid")) prefs.putString("ssid", server.arg("ssid"));
    if(server.hasArg("pass")) prefs.putString("pass", server.arg("pass"));
    if(server.hasArg("web_pw")) {
      String newWebPw = server.arg("web_pw");
      if(newWebPw.length() >= 6) {  // Minimum 6 characters
        prefs.putString("web_pw", newWebPw);
      }
    }
    if(server.hasArg("http_url")) prefs.putString("http_url", server.arg("http_url"));
    if(server.hasArg("mq_srv")) prefs.putString("mq_srv", server.arg("mq_srv"));
    if(server.hasArg("mq_prt")) prefs.putInt("mq_prt", server.arg("mq_prt").toInt());
    if(server.hasArg("mq_usr")) prefs.putString("mq_usr", server.arg("mq_usr"));
    if(server.hasArg("mq_pw")) prefs.putString("mq_pw", server.arg("mq_pw"));
    if(server.hasArg("mq_top")) prefs.putString("mq_top", server.arg("mq_top"));
    if(server.hasArg("mq_qos")) {
       uint8_t qos = server.arg("mq_qos").toInt();
       if(qos <= 2) prefs.putUChar("mq_qos", qos);
    }
    if(server.hasArg("mq_kalive")) {
       uint16_t keepalive = server.arg("mq_kalive").toInt();
       if(keepalive >= 10 && keepalive <= 300) prefs.putUShort("mq_kalive", keepalive);
    }
    if(server.hasArg("lwt_top")) prefs.putString("lwt_top", server.arg("lwt_top"));
    if(server.hasArg("lwt_off")) prefs.putString("lwt_off", server.arg("lwt_off"));
    if(server.hasArg("lwt_on")) prefs.putString("lwt_on", server.arg("lwt_on"));
    if(server.hasArg("lwt_qos")) {
       uint8_t lwt_qos = server.arg("lwt_qos").toInt();
       if(lwt_qos <= 2) prefs.putUChar("lwt_qos", lwt_qos);
    }
    prefs.putBool("lwt_ret", server.hasArg("lwt_ret"));
    if(server.hasArg("aes")) prefs.putString("aes", server.arg("aes"));
    if(server.hasArg("magic")) {
       uint16_t m = strtol(server.arg("magic").c_str(), NULL, 16);
       prefs.putUInt("magic", m);
    }
    if(server.hasArg("lora_prf")) {
       uint8_t profile = server.arg("lora_prf").toInt();
       if(profile >= 1 && profile <= 4) {
         prefs.putUChar("lora_prf", profile);
       }
    }
    prefs.end();
    server.send(200, "text/html", "<h2>Saved! Rebooting...</h2>");
    delay(1000);
    ESP.restart();
  });

  // GET /reset — reboot gateway
  server.on("/reset", HTTP_GET, []() {
    if (!checkAuth()) return;

    server.send(200, "text/html", "<html><head><meta charset='UTF-8'></head><body><h2>Rebooting Gateway...</h2><p>Please wait 10 seconds...</p></body></html>");
    delay(1000);
    ESP.restart();
  });

  // POST /update — OTA firmware upload (watchdog fed on each chunk)
  server.on("/update", HTTP_POST, []() {
    if (!checkAuth()) return;

    otaInProgress = false;

    if (Update.hasError()) {
      server.send(500, "text/plain", "OTA FAILED! Check serial output.");
      Serial.println("OTA Update FAILED!");
      Update.printError(Serial);
    } else {
      server.send(200, "text/plain", "SUCCESS! Gateway rebooting in 3 seconds...");
      Serial.println("OTA Update SUCCESS! Rebooting...");
      delay(3000);  // Give time for HTTP response to complete
      ESP.restart();
    }
  }, []() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      otaInProgress = true;
      Serial.printf("\n*** OTA UPDATE STARTED ***\n");
      Serial.printf("Filename: %s\n", upload.filename.c_str());

      triggerBacklight();
      tft.fillScreen(ST77XX_BLUE);
      tft.setTextColor(ST77XX_WHITE);
      tft.setTextSize(3);
      tft.setCursor(30, 100);
      tft.println("OTA UPDATE");
      tft.setTextSize(2);
      tft.setCursor(40, 140);
      tft.println("Uploading...");

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        otaInProgress = false;
      }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
      esp_task_wdt_reset();
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }

      static size_t lastProgress = 0;
      size_t progress = Update.progress();
      size_t totalSize = Update.size();

      if (totalSize > 0 && (progress - lastProgress > 50000 || progress == totalSize)) {
        int percent = (int)((progress * 100) / totalSize);
        Serial.printf("OTA Progress: %d / %d bytes (%d%%)\n", progress, totalSize, percent);

        tft.fillRect(20, 180, 200, 20, ST77XX_BLACK);
        tft.fillRect(20, 180, (200 * percent) / 100, 20, ST77XX_GREEN);

        tft.fillRect(85, 205, 70, 20, ST77XX_BLUE);
        tft.setCursor(90, 210);
        tft.setTextSize(2);
        tft.setTextColor(ST77XX_WHITE);
        tft.printf("%d%%", percent);

        lastProgress = progress;
      }

    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("\nOTA Upload Complete: %d bytes\n", upload.totalSize);
        Serial.println("Firmware flashed successfully!");

        tft.fillScreen(ST77XX_GREEN);
        tft.setTextColor(ST77XX_BLACK);
        tft.setTextSize(3);
        tft.setCursor(40, 100);
        tft.println("SUCCESS!");
        tft.setTextSize(2);
        tft.setCursor(30, 140);
        tft.println("Rebooting...");
      } else {
        Update.printError(Serial);

        tft.fillScreen(ST77XX_RED);
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(3);
        tft.setCursor(50, 100);
        tft.println("FAILED!");
        tft.setTextSize(1);
        tft.setCursor(20, 140);
        tft.println("Check serial output");
      }

    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.end();
      Serial.println("OTA Update ABORTED!");
      otaInProgress = false;
    }
  });
}

// ============================================================================
// MQTT CONNECTION MANAGEMENT
// ============================================================================

// Connects to MQTT broker with LWT registration. Publishes retained "online"
// status on success. Supports authenticated and anonymous connections.
bool connectMQTT() {
  if (cfg_mqtt_server == "") return false;
  if (mqtt.connected()) return true;

  String clientId = "S3-GW-" + gwID;
  bool connected = false;

  mqtt.setKeepAlive(cfg_mqtt_keepalive);
  Serial.printf("MQTT Keepalive set to %d seconds\n", cfg_mqtt_keepalive);

  if (cfg_mqtt_user != "") {
    connected = mqtt.connect(
      clientId.c_str(),
      cfg_mqtt_user.c_str(),
      cfg_mqtt_pass.c_str(),
      cfg_mqtt_lwt_topic.c_str(),
      cfg_mqtt_lwt_qos,
      cfg_mqtt_lwt_retain,
      cfg_mqtt_lwt_msg_offline.c_str()
    );
  } else {
    connected = mqtt.connect(
      clientId.c_str(),
      cfg_mqtt_lwt_topic.c_str(),
      cfg_mqtt_lwt_qos,
      cfg_mqtt_lwt_retain,
      cfg_mqtt_lwt_msg_offline.c_str()
    );
  }

  if (connected) {
    Serial.println("MQTT Connected with LWT");

    mqtt.publish(
      cfg_mqtt_lwt_topic.c_str(),
      cfg_mqtt_lwt_msg_online.c_str(),
      cfg_mqtt_lwt_retain
    );
    Serial.println("Published online status");
  } else {
    Serial.print("MQTT Connect Failed RC=");
    Serial.println(mqtt.state());
  }

  return connected;
}
