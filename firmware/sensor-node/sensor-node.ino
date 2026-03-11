// AetherLogic Sensor Node Firmware
// IoT air quality monitoring for industrial aluminum powder coating environments
// Part of undergraduate thesis (Polytechnic School)
//
// Hardware: ESP32-C3 + SX1276 LoRa + BME680 + SCD40 + ZH03B + Mic
// Cycle: Wake -> Measure -> Encrypt -> LoRa TX -> Deep Sleep

#include <math.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include "SparkFun_SCD4x_Arduino_Library.h"
#include <HardwareSerial.h>
#include <WiFi.h>
#include "mbedtls/aes.h"
#include <driver/gpio.h>
#include <Preferences.h>

// --- Pin Assignments (ESP32-C3) ---
#define BAT_PIN       0    // ADC1_CH0 - Battery voltage divider
#define MIC_PIN       1    // ADC1_CH1 - Microphone amplifier output

#define ZH_PWR_PIN    5    // 5V boost enable (HIGH = ON)
#define SENS_PWR_PIN  8    // P-MOSFET gate for 3.3V sensor rail (LOW = ON)

#define I2C_SDA       19
#define I2C_SCL       18

#define ZH_RX_PIN     4    // ZH03B UART RX

#define LORA_SCK      6
#define LORA_MISO     2
#define LORA_MOSI     7
#define LORA_CS       10
#define LORA_RST      9
#define LORA_DIO0     3

// --- Default Configuration (used on first boot, overridden by NVS) ---
#define DEFAULT_ID      1
#define DEFAULT_NAME    "Node"
#define DEFAULT_SLEEP   300        // seconds
#define DEFAULT_WARMUP  25         // seconds
#define DEFAULT_VOLT_F  2.6        // ADC mV to battery V factor
#define DEFAULT_DB_OFF  68.0       // Microphone dB calibration offset
#define DEFAULT_TMP_OFF 0.0        // Temperature offset (°C)
#define DEFAULT_CO2_OFF 0          // CO2 offset (ppm)
#define DEFAULT_AES     "MySecretKey12345"  // AES-128 key (16 bytes)
#define MAGIC_NUMBER    0xCAFE     // Packet header magic
#define DEFAULT_BASELINE 50000.0   // Initial BME680 gas baseline (Ohm)
#define DEFAULT_LORA_PROFILE 2     // BALANCED

// LoRa Profiles: 1=ECO(SF7,14dBm) 2=BALANCED(SF9,17dBm) 3=LONG(SF10,20dBm) 4=EXTREME(SF12,20dBm)

// --- Global Objects ---
Preferences prefs;
Adafruit_BME680 bme;
SCD4x scd4x;
HardwareSerial zhSerial(1);

// Runtime config (loaded from NVS)
uint16_t cfg_deviceId;
String   cfg_deviceName;
uint32_t cfg_sleepSec;
uint32_t cfg_warmupSec;
float    cfg_voltFactor;
float    cfg_dbOffset;
float    cfg_tempOffset;
float    cfg_pressOffset;
int16_t  cfg_co2Offset;
String   cfg_aesKey;
float    current_baseline;
uint8_t  cfg_loraProfile;

uint32_t currentPacketId = 0;

// Retained across deep sleep (not across power loss)
RTC_DATA_ATTR int decay_counter = 0;

// LoRa payload: 48 bytes = 3x AES-128 blocks, no padding needed
struct SecurePacket {
  uint16_t magic;
  uint16_t deviceId;
  uint32_t packetId;
  float voltage;
  float noise_db;
  float temp;
  float hum;
  float pressure;
  float iaq;
  uint16_t co2;
  uint16_t pm1;
  uint16_t pm25;
  uint16_t pm10;
  float tvoc;
  uint8_t padding[4];
};

struct DustData {
  uint16_t pm1_0;
  uint16_t pm2_5;
  uint16_t pm10;
};

// --- IAQ Calculation ---
// Composite air quality score (25=best, 500=worst) from BME680 gas resistance
// and humidity. Based on Bosch application note approach (without BSEC library).
//
// Humidity contributes 25% (optimal band: 38-42% RH).
// Gas resistance contributes 75%, normalized against a dynamic baseline
// that tracks the highest resistance seen in clean air.
float calculateIAQ(float gasResistance, float humidity, float baseline) {
  float hum_reference = 40.0;
  float hum_weighting = 0.25;
  float gas_weighting = 0.75;

  float current_hum_contribution = 0;
  if (humidity >= 38 && humidity <= 42)
    current_hum_contribution = 0.25 * 100;
  else {
    if (humidity < 38) current_hum_contribution = 0.25/hum_reference * humidity * 100;
    else current_hum_contribution = ((-0.25/(100-hum_reference) * humidity) + 0.416666) * 100;
  }

  float current_gas_contribution = 0;

  if (baseline < 5000) baseline = 5000;
  if (gasResistance > baseline) gasResistance = baseline;

  current_gas_contribution = (gas_weighting / (baseline - 5000) * (gasResistance - 5000)) * 100;

  float iaq_score = current_hum_contribution + current_gas_contribution;

  float final_iaq = 500.0 - (iaq_score * 4.75);
  if (final_iaq < 25) final_iaq = 25;

  return final_iaq;
}

// Configure SX1276 based on selected profile (1-4)
void applyLoRaProfile(uint8_t profile) {
  switch(profile) {
    case 1:
      LoRa.setSpreadingFactor(7);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(5);
      LoRa.setTxPower(14);
      Serial.println("LoRa Profile: ECO (SF7, 14dBm)");
      Serial.println("  Range: ~2km | TX Time: ~50ms | Power: LOW");
      break;

    case 2:
      LoRa.setSpreadingFactor(9);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(5);
      LoRa.setTxPower(17);
      Serial.println("LoRa Profile: LONG (SF10, 17dBm)");
      Serial.println("  Range: ~4km | TX Time: ~200ms | Power: MEDIUM");
      break;

    case 3:
      LoRa.setSpreadingFactor(10);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(6);
      LoRa.setTxPower(20);
      Serial.println("LoRa Profile: LONG (SF10, 20dBm)");
      Serial.println("  Range: ~8km | TX Time: ~400ms | Power: HIGH");
      break;

    case 4:
      LoRa.setSpreadingFactor(12);
      LoRa.setSignalBandwidth(125E3);
      LoRa.setCodingRate4(8);
      LoRa.setTxPower(20);
      Serial.println("LoRa Profile: EXTREME (SF12, 20dBm)");
      Serial.println("  Range: ~15km | TX Time: ~1.5s | Power: VERY HIGH");
      break;

    default:
      profile = 3;
      applyLoRaProfile(3);
      return;
  }

  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
}

// Load all config from NVS, use defaults on first boot
void loadSettings() {
  prefs.begin("node_cfg", true);
  cfg_deviceId   = prefs.getUShort("dev_id", DEFAULT_ID);
  cfg_deviceName = prefs.getString("dev_name", DEFAULT_NAME);
  cfg_sleepSec   = prefs.getUInt("sleep_s", DEFAULT_SLEEP);
  cfg_warmupSec  = prefs.getUInt("warm_s", DEFAULT_WARMUP);
  cfg_voltFactor = prefs.getFloat("v_fact", DEFAULT_VOLT_F);
  cfg_dbOffset   = prefs.getFloat("db_off", DEFAULT_DB_OFF);
  cfg_tempOffset = prefs.getFloat("t_off", DEFAULT_TMP_OFF);
  cfg_pressOffset = prefs.getFloat("p_off", 0.0);
  cfg_co2Offset  = prefs.getShort("co2_off", DEFAULT_CO2_OFF);
  cfg_aesKey     = prefs.getString("aes_key", DEFAULT_AES);
  cfg_loraProfile = prefs.getUChar("lora_prf", DEFAULT_LORA_PROFILE);

  current_baseline = prefs.getFloat("gas_base", DEFAULT_BASELINE);
  currentPacketId  = prefs.getUInt("pkt_cnt", 0);

  prefs.end();

  if (cfg_aesKey.length() != 16) cfg_aesKey = DEFAULT_AES;
  if (cfg_loraProfile < 1 || cfg_loraProfile > 4) cfg_loraProfile = 2;
}

void savePacketCounter(uint32_t cnt) {
  prefs.begin("node_cfg", false);
  prefs.putUInt("pkt_cnt", cnt);
  prefs.end();
}

void saveBaseline(float newBaseline) {
  prefs.begin("node_cfg", false);
  prefs.putFloat("gas_base", newBaseline);
  prefs.end();
  Serial.printf("Auto-Calib: New Gas Baseline Saved: %.0f Ohm\n", newBaseline);
}

// Interactive serial configuration menu with optional calibration wizard.
// Press 'c' during the 3-second boot window to enter.
void configureMode() {
  while(Serial.available()) Serial.read();

  Serial.println("\n--- CONFIGURATION MENU ---");
  Serial.println("Note: Press ENTER to keep current value.");
  Serial.setTimeout(1000);

  prefs.begin("node_cfg", false);

  auto readInput = [](String prompt, String currentVal) -> String {
    Serial.print(prompt + " [" + currentVal + "]: ");

    unsigned long start = millis();
    while (!Serial.available()) {
        if (millis() - start > 60000) return currentVal;
        delay(10);
    }

    String str = Serial.readStringUntil('\n');
    str.trim();

    if (str.length() == 0) {
       Serial.println("Kept");
       return currentVal;
    }
    Serial.println(str);
    return str;
  };

  String input;

  input = readInput("Device ID (Numeric)", String(cfg_deviceId));
  if(input != "") prefs.putUShort("dev_id", (uint16_t)input.toInt());

  input = readInput("Device Name (e.g. Office 1)", cfg_deviceName);
  if(input != "") prefs.putString("dev_name", input);

  input = readInput("Sleep (sec)", String(cfg_sleepSec));
  if(input != "") prefs.putUInt("sleep_s", (uint32_t)input.toInt());

  input = readInput("Warmup (sec)", String(cfg_warmupSec));
  if(input != "") prefs.putUInt("warm_s", (uint32_t)input.toInt());

  // Calibration wizard: single-point offset calibration against reference instruments
  Serial.println("\n=== SENSOR CALIBRATION ===");
  input = readInput("Run Calibration Wizard? (y/n)", "n");

  if(input == "y") {
    Serial.println("\nPowering ON sensors for calibration...");

    pinMode(ZH_PWR_PIN, OUTPUT);
    digitalWrite(ZH_PWR_PIN, HIGH);
    pinMode(SENS_PWR_PIN, OUTPUT);
    digitalWrite(SENS_PWR_PIN, LOW);
    delay(500);

    Wire.begin(I2C_SDA, I2C_SCL);
    pinMode(MIC_PIN, INPUT);
    pinMode(BAT_PIN, INPUT);
    analogSetAttenuation(ADC_11db);
    delay(100);

    // Battery voltage - calibrate divider factor against multimeter
    Serial.println("\n--- Battery Voltage Calibration ---");
    uint32_t raw_bat = analogReadMilliVolts(BAT_PIN);
    float measured_v = (raw_bat * cfg_voltFactor) / 1000.0;
    Serial.printf("Current reading: %.2f V (raw: %d mV)\n", measured_v, raw_bat);
    Serial.printf("Current factor: %.2f\n", cfg_voltFactor);

    input = readInput("Enter TRUE voltage (from multimeter)", "");
    if(input != "") {
      float true_voltage = input.toFloat();
      float new_factor = (true_voltage * 1000.0) / raw_bat;
      prefs.putFloat("v_fact", new_factor);
      Serial.printf("New voltage factor: %.2f\n", new_factor);
      Serial.printf("Sensor will now read: %.2f V\n", true_voltage);
      cfg_voltFactor = new_factor;
    }

    // Temperature & Pressure (BME680)
    if (bme.begin() || bme.begin(0x76)) {
      bme.setTemperatureOversampling(BME680_OS_8X);
      bme.setHumidityOversampling(BME680_OS_2X);
      bme.setPressureOversampling(BME680_OS_16X);
      bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
      bme.setGasHeater(320, 150);

      delay(2000);
      bme.performReading();

      Serial.println("\n--- Temperature Calibration ---");
      float measured_temp = bme.temperature;
      Serial.printf("Current reading: %.2f °C\n", measured_temp);
      Serial.printf("Current offset: %.2f °C\n", cfg_tempOffset);
      Serial.printf("Corrected: %.2f °C\n", measured_temp + cfg_tempOffset);

      input = readInput("Enter TRUE temperature (from reference thermometer)", "");
      if(input != "") {
        float true_temp = input.toFloat();
        float new_offset = true_temp - measured_temp;
        prefs.putFloat("t_off", new_offset);
        Serial.printf("New temp offset: %.2f °C\n", new_offset);
        Serial.printf("Sensor will now read: %.2f °C\n", measured_temp + new_offset);
        cfg_tempOffset = new_offset;
      }

      Serial.println("\n--- Pressure Calibration ---");
      Serial.println("Note: Your barometer may be faulty (~698 hPa)");
      float measured_press = bme.pressure / 100.0;
      Serial.printf("Current reading: %.2f hPa\n", measured_press);
      Serial.printf("Current offset: %.2f hPa\n", cfg_pressOffset);

      input = readInput("Enter TRUE pressure (from weather station)", "");
      if(input != "") {
        float true_pressure = input.toFloat();
        float press_offset = true_pressure - measured_press;
        prefs.putFloat("p_off", press_offset);
        Serial.printf("New pressure offset: %.2f hPa\n", press_offset);
        Serial.printf("Sensor will now read: %.2f hPa\n", measured_press + press_offset);
        cfg_pressOffset = press_offset;
      }
    } else {
      Serial.println("\nBME680 not found - skipping temp/pressure calibration");
    }

    // CO2 (SCD40)
    Serial.println("\n--- CO2 Calibration (SCD40) ---");
    if (scd4x.begin()) {
      scd4x.startPeriodicMeasurement();
      Serial.println("Waiting 5 sec for measurement...");
      delay(5000);

      if (scd4x.readMeasurement()) {
        uint16_t measured_co2 = scd4x.getCO2();
        Serial.printf("Current reading: %d ppm\n", measured_co2);
        Serial.printf("Current offset: %d ppm\n", cfg_co2Offset);
        Serial.printf("Corrected: %d ppm\n", measured_co2 + cfg_co2Offset);
        Serial.println("Tip: Fresh outdoor air = 400-420 ppm");

        input = readInput("Enter TRUE CO2 level (ppm)", "");
        if(input != "") {
          int16_t true_co2 = input.toInt();
          int16_t new_offset = true_co2 - measured_co2;
          prefs.putShort("co2_off", new_offset);
          Serial.printf("New CO2 offset: %d ppm\n", new_offset);
          Serial.printf("Sensor will now read: %d ppm\n", measured_co2 + new_offset);
          cfg_co2Offset = new_offset;
        }
      }
      scd4x.stopPeriodicMeasurement();
    } else {
      Serial.println("SCD40 not found!");
    }

    // Microphone dB calibration - sample for 5s, compare with SPL meter
    Serial.println("\n--- Microphone dB Calibration ---");
    Serial.println("Measuring for 5 seconds...");
    Serial.println("(Use calibrated SPL meter as reference)");

    double sum_db = 0.0;
    int count = 0;

    for(int i=0; i<100; i++) {
      unsigned long micStart = millis();
      unsigned int signalMax = 0;
      unsigned int signalMin = 4095;

      while (millis() - micStart < 50) {
        unsigned int sample = analogRead(MIC_PIN);
        if (sample < 4095) {
          if (sample > signalMax) signalMax = sample;
          if (sample < signalMin) signalMin = sample;
        }
      }

      double volts_mic = ((signalMax - signalMin) * 2.5) / 4095.0;
      double db = 20.0 * log10(volts_mic + 0.001) + cfg_dbOffset;
      sum_db += db;
      count++;
    }

    float avg_db = sum_db / count;
    Serial.printf("Current reading: %.1f dB\n", avg_db);
    Serial.printf("Current offset: %.1f\n", cfg_dbOffset);

    input = readInput("Enter TRUE dB level (from SPL meter)", "");
    if(input != "") {
      float true_db = input.toFloat();
      float new_offset = true_db - (avg_db - cfg_dbOffset);
      prefs.putFloat("db_off", new_offset);
      Serial.printf("New dB offset: %.1f\n", new_offset);
      cfg_dbOffset = new_offset;
    }

    digitalWrite(ZH_PWR_PIN, LOW);
    digitalWrite(SENS_PWR_PIN, HIGH);

    Serial.println("\n=== Calibration Complete ===\n");
  } else {
    // Manual offset entry without live sensors
    Serial.println("\nManual offset configuration:");

    input = readInput("Volt Factor", String(cfg_voltFactor));
    if(input != "") prefs.putFloat("v_fact", input.toFloat());

    input = readInput("Temp Offset", String(cfg_tempOffset));
    if(input != "") prefs.putFloat("t_off", input.toFloat());

    input = readInput("Pressure Offset", String(cfg_pressOffset));
    if(input != "") prefs.putFloat("p_off", input.toFloat());

    input = readInput("dB Offset", String(cfg_dbOffset));
    if(input != "") prefs.putFloat("db_off", input.toFloat());

    input = readInput("CO2 Offset", String(cfg_co2Offset));
    if(input != "") prefs.putShort("co2_off", (int16_t)input.toInt());
  }

  input = readInput("AES Key (16 chars)", cfg_aesKey);
  if (input.length() == 16) prefs.putString("aes_key", input);

  Serial.println("\n--- LoRa Profile Selection ---");
  Serial.println("1: ECO      - ~2km range, LOW power (SF7, 14dBm)");
  Serial.println("2: BALANCED - ~4km range, MEDIUM power (SF9, 17dBm) [DEFAULT]");
  Serial.println("3: LONG     - ~8km range, HIGH power (SF10, 20dBm)");
  Serial.println("4: EXTREME  - ~15km range, VERY HIGH power (SF12, 20dBm)");
  Serial.printf("Current Profile: %d ", cfg_loraProfile);
  switch(cfg_loraProfile) {
    case 1: Serial.println("(ECO)"); break;
    case 2: Serial.println("(BALANCED)"); break;
    case 3: Serial.println("(LONG)"); break;
    case 4: Serial.println("(EXTREME)"); break;
  }
  input = readInput("LoRa Profile (1-4)", String(cfg_loraProfile));
  if(input != "") {
    uint8_t profile = input.toInt();
    if(profile >= 1 && profile <= 4) {
      prefs.putUChar("lora_prf", profile);
      Serial.printf("LoRa Profile set to %d\n", profile);
    } else {
      Serial.println("Invalid! Keeping current.");
    }
  }

  Serial.printf("\nCurrent Packet Count: %d\n", currentPacketId);
  input = readInput("Reset Packet Counter to 0? (y/n)", "n");
  if(input == "y") {
    prefs.putUInt("pkt_cnt", 0);
    Serial.println("Packet Counter Reset to 0!");
  }

  Serial.printf("Current Gas Baseline: %.0f\n", current_baseline);
  input = readInput("Reset Baseline to Default? (y/n)", "n");
  if(input == "y") {
    prefs.putFloat("gas_base", DEFAULT_BASELINE);
    Serial.println("Baseline Reset!");
  }

  Serial.println("\nSaved. Restarting...");
  prefs.end();
  delay(1000);
  ESP.restart();
}

// AES-128-ECB encrypt the 48-byte packet (3 blocks) and send over LoRa
void encryptAndSend(SecurePacket* pkt) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  uint8_t keyBytes[16];
  cfg_aesKey.getBytes(keyBytes, 17);
  mbedtls_aes_setkey_enc(&aes, keyBytes, 128);

  uint8_t input[48];
  uint8_t output[48];
  memcpy(input, pkt, 48);

  for(int i=0; i<3; i++) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + (i*16), output + (i*16));
  }
  mbedtls_aes_free(&aes);

  LoRa.beginPacket();
  LoRa.write(output, 48);
  LoRa.endPacket();
}

// Parse ZH03B 32-byte Plantower-compatible UART frame.
// Header: 0x42 0x4D, then bytes 8-13 = atmospheric PM values (ug/m3).
// Falls back to CF=1 standard values (bytes 2-7) if atmospheric reads zero.
DustData readZH03B() {
  DustData d = {0, 0, 0};
  if (!zhSerial.available()) return d;
  byte buffer[32];
  unsigned long start = millis();
  while (millis() - start < 200 && zhSerial.available()) {
    if (zhSerial.read() == 0x42) {
      if (zhSerial.peek() == 0x4D) {
        zhSerial.read();
        if (zhSerial.available() >= 22) {
           zhSerial.readBytes(buffer, 22);
           d.pm1_0 = (buffer[8] << 8) + buffer[9];
           d.pm2_5 = (buffer[10] << 8) + buffer[11];
           d.pm10  = (buffer[12] << 8) + buffer[13];
           if (d.pm2_5 == 0 && (buffer[2] != 0 || buffer[3] != 0)) {
              d.pm1_0 = (buffer[2] << 8) + buffer[3];
              d.pm2_5 = (buffer[4] << 8) + buffer[5];
              d.pm10  = (buffer[6] << 8) + buffer[7];
           }
           return d;
        }
      }
    }
  }
  return d;
}

// setup() is the entire operational cycle - loop() is never reached
// because deep sleep resets the CPU back to setup() on each wake.
void setup() {
  // Release GPIO hold from previous deep sleep
  gpio_hold_dis((gpio_num_t)ZH_PWR_PIN);
  gpio_hold_dis((gpio_num_t)SENS_PWR_PIN);
  gpio_deep_sleep_hold_dis();

  WiFi.mode(WIFI_OFF);
  btStop();

  Serial.begin(115200);
  delay(2000);

  loadSettings();

  // 3-second window to enter config menu
  Serial.println("Press 'c' to configure...");
  unsigned long menuStart = millis();
  while(millis() - menuStart < 3000) {
    if(Serial.available() && Serial.read() == 'c') {
       configureMode();
    }
  }

  Serial.printf("Starting Measurement Cycle for: %s (ID: %d)\n", cfg_deviceName.c_str(), cfg_deviceId);

  if (cfg_warmupSec < 5) {
    Serial.println("WARNING: Warmup time < 5s - SCD40 may not be ready!");
    Serial.println("  Recommended: 25-30s for CO2, PM, and VOC sensors");
  } else if (cfg_warmupSec < 25) {
    Serial.println("WARNING: Warmup time < 25s - PM sensor may be unstable!");
    Serial.println("  Recommended: 25-30s for optimal readings");
  }

  // Measure battery BEFORE powering sensors (unloaded voltage = better SoC estimate)
  analogSetAttenuation(ADC_11db);
  pinMode(BAT_PIN, INPUT);
  delay(10);
  uint32_t raw_bat = analogReadMilliVolts(BAT_PIN);
  float voltage = (raw_bat * cfg_voltFactor) / 1000.0;
  Serial.printf("Bat: %.2f V\n", voltage);

  // Power on sensors: 5V boost for ZH03B, P-MOSFET for 3.3V rail
  pinMode(ZH_PWR_PIN, OUTPUT);
  digitalWrite(ZH_PWR_PIN, HIGH);
  pinMode(SENS_PWR_PIN, OUTPUT);
  digitalWrite(SENS_PWR_PIN, LOW);
  delay(500);

  // Init peripherals
  pinMode(MIC_PIN, INPUT);
  Wire.begin(I2C_SDA, I2C_SCL);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(868E6)) {
    Serial.println("LoRa Fail");
  } else {
    applyLoRaProfile(cfg_loraProfile);
  }

  // BME680: try default addr 0x77, then alternate 0x76
  bool bmeOK = false;
  if (bme.begin()) {
    bmeOK = true;
    Serial.println("BME680 found (default addr)");
  } else if (bme.begin(0x76)) {
    bmeOK = true;
    Serial.println("BME680 found @ 0x76");
  } else {
    Serial.println("BME680 NOT FOUND - Skipping!");
  }

  if (bmeOK) {
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_16X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);
  }

  if (scd4x.begin()) {
    scd4x.startPeriodicMeasurement();
    Serial.println("SCD40 started (needs 5s warmup for CO2)");
  } else {
    Serial.println("SCD4x Not Found");
  }

  zhSerial.begin(9600, SERIAL_8N1, ZH_RX_PIN, -1);
  Serial.println("ZH03B started (needs 30s warmup for stable PM readings)");

  // Warm-up: cycle BME680 heater every 2s while sampling mic for noise average
  Serial.printf("Warming up sensors for %d seconds...\n", cfg_warmupSec);
  double sum_db = 0.0;
  int samples_count = 0;
  unsigned long warmupStart = millis();
  unsigned long warmupMs = cfg_warmupSec * 1000UL;

  unsigned long lastBmeRead = 0;
  int bme_warmup_count = 0;

  while (millis() - warmupStart < warmupMs) {
    if (bmeOK && (millis() - lastBmeRead >= 2000)) {
      if (bme.performReading()) {
        bme_warmup_count++;
        Serial.printf("  BME warmup #%d: Gas %.0f Ohm, Temp %.1f C\n",
                      bme_warmup_count, bme.gas_resistance, bme.temperature);
      } else {
        Serial.printf("  BME warmup attempt failed (sensor not ready)\n");
      }
      lastBmeRead = millis();
    }

    // 50ms mic sampling window, peak-to-peak -> dB
    unsigned long micStart = millis();
    unsigned int signalMax = 0;
    unsigned int signalMin = 4095;
    while (millis() - micStart < 50) {
      unsigned int sample = analogRead(MIC_PIN);
      if (sample < 4095) {
        if (sample > signalMax) signalMax = sample;
        if (sample < signalMin) signalMin = sample;
      }
    }
    double volts_mic = ((signalMax - signalMin) * 2.5) / 4095.0;
    double db = 20.0 * log10(volts_mic + 0.001) + cfg_dbOffset;
    sum_db += db;
    samples_count++;
  }

  Serial.printf("Warmup complete: %d BME readings, %d noise samples\n",
                bme_warmup_count, samples_count);

  // Final sensor readout
  double avg_db = (samples_count > 0) ? (sum_db / samples_count) : 0.0;

  if (bmeOK) {
    bme.performReading();

    // Dynamic IAQ baseline calibration with exponential decay.
    // If gas resistance > baseline -> cleaner air detected, update baseline immediately.
    // If gas resistance < baseline -> polluted, decay baseline by 0.05% per cycle
    // to prevent a single high reading from permanently inflating it ("ratchet effect").
    // NVS writes batched every 20 cycles during decay to reduce flash wear.
    if (current_baseline < 5000) {
      current_baseline = bme.gas_resistance;
      Serial.printf("Cold Start: Baseline initialized to %.0f Ohm\n", current_baseline);
    }

    float gas_offset = current_baseline - bme.gas_resistance;

    if (gas_offset > 0) {
      current_baseline *= 0.9995;

      if (++decay_counter >= 20) {
        saveBaseline(current_baseline);
        decay_counter = 0;
      }

      Serial.printf("Air Quality: POLLUTED (Gas %.0f < Baseline %.0f, Δ=%.0f, Decay#%d)\n",
                    bme.gas_resistance, current_baseline, gas_offset, decay_counter);
    } else {
      current_baseline = bme.gas_resistance;
      saveBaseline(current_baseline);
      decay_counter = 0;

      Serial.printf("Air Quality: CLEAN (New baseline: %.0f Ohm)\n", current_baseline);
    }

    if (current_baseline < 5000) current_baseline = 5000;

  } else {
    Serial.println("BME: Skipped (not initialized)");
  }

  uint16_t co2 = 0;
  if (scd4x.readMeasurement()) co2 = scd4x.getCO2();

  DustData dust = readZH03B();

  currentPacketId++;
  savePacketCounter(currentPacketId);

  // Build packet, apply offsets, encrypt, transmit
  SecurePacket pkt;
  pkt.magic = MAGIC_NUMBER;
  pkt.deviceId = cfg_deviceId;
  pkt.packetId = currentPacketId;
  pkt.voltage = voltage;
  pkt.noise_db = (float)avg_db;

  if (bmeOK) {
    pkt.temp = bme.temperature + cfg_tempOffset;
    pkt.hum = bme.humidity;
    pkt.pressure = (bme.pressure / 100.0) + cfg_pressOffset;

    Serial.printf("BME DEBUG: Pressure RAW = %u Pa, Converted = %.2f hPa, Final = %.2f hPa\n",
                  bme.pressure, bme.pressure / 100.0, pkt.pressure);
  } else {
    pkt.temp = 0;
    pkt.hum = 0;
    pkt.pressure = 0;
  }

  pkt.iaq = calculateIAQ(bme.gas_resistance, bme.humidity, current_baseline);
  pkt.tvoc = bme.gas_resistance / 1000.0;
  pkt.co2 = co2 + cfg_co2Offset;
  pkt.pm1 = dust.pm1_0;
  pkt.pm25 = dust.pm2_5;
  pkt.pm10 = dust.pm10;

  Serial.printf("BME: Gas %.2f kOhm (Base %.0f) -> IAQ %.0f\n",
                pkt.tvoc, current_baseline, pkt.iaq);

  Serial.printf("[%s] TX ID:%d #%d | V:%.2f | dB:%.1f | T:%.1f H:%.0f%% P:%.0f | IAQ:%.0f Gas:%.2fk | CO2:%d | PM1:%d PM2.5:%d PM10:%d\n",
                cfg_deviceName.c_str(), pkt.deviceId, pkt.packetId, pkt.voltage, pkt.noise_db,
                pkt.temp, pkt.hum, pkt.pressure,
                pkt.iaq, pkt.tvoc, pkt.co2, pkt.pm1, pkt.pm25, pkt.pm10);

  encryptAndSend(&pkt);

  // Shutdown: LoRa sleep, ground all GPIOs to prevent parasitic leakage,
  // disconnect sensor power, lock GPIO states for deep sleep
  LoRa.sleep();

  const uint8_t pinsToGround[] = {
    BAT_PIN, MIC_PIN, I2C_SDA, I2C_SCL, ZH_RX_PIN,
    LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS, LORA_RST, LORA_DIO0
  };

  for(int i=0; i<sizeof(pinsToGround); i++) {
    pinMode(pinsToGround[i], OUTPUT);
    digitalWrite(pinsToGround[i], LOW);
  }

  digitalWrite(ZH_PWR_PIN, LOW);       // 5V boost OFF
  digitalWrite(SENS_PWR_PIN, HIGH);    // P-MOSFET OFF (active-low)

  gpio_hold_en((gpio_num_t)ZH_PWR_PIN);
  gpio_hold_en((gpio_num_t)SENS_PWR_PIN);
  gpio_deep_sleep_hold_en();

  // Random jitter 0-5s to avoid LoRa collisions between nodes
  uint32_t jitter = esp_random() % 5000;
  uint64_t totalSleepUs = (cfg_sleepSec * 1000000ULL) + (jitter * 1000ULL);

  Serial.printf("Sleep %d s\n", cfg_sleepSec);
  Serial.flush();

  esp_sleep_enable_timer_wakeup(totalSleepUs);
  esp_deep_sleep_start();
}

void loop() {}
