# Pinouts Documentation

## Sensor Node - ESP32-C3

### Analog Inputs (ADC1)
| Λειτουργία | ESP32-C3 Pin | Σημείωση |
|------------|--------------|----------|
| Battery    | GPIO0        | ADC1_CH0 |
| Microphone | GPIO1        | ADC1_CH1 |

### Power Control
| Λειτουργία   | ESP32-C3 Pin | Σημείωση |
|--------------|--------------|----------|
| ZH03B Power  | GPIO5        | Boost EN |
| Sensors Power| GPIO8        | P-MOS Gate |

### I2C Bus (BME680, SCD40)
| Σήμα | ESP32-C3 Pin |
|------|--------------|
| SDA  | GPIO19       |
| SCL  | GPIO18       |

### ZH03B (UART - RX only)
| ZH03B Pin | ESP32-C3 Pin | Σημείωση |
|-----------|--------------|----------|
| TXD       | GPIO4        | ZH_RX_PIN |
| VCC       | 5V (via boost) | Ελέγχεται από GPIO5 |
| GND       | GND          |          |

### LoRa Module (SX1276)
| SX1276 Pin | ESP32-C3 Pin | Σημείωση |
|------------|--------------|----------|
| VCC        | 3.3V         |          |
| GND        | GND          |          |
| SCK        | GPIO6        | SPI CLK  |
| MISO       | GPIO2        | SPI MISO |
| MOSI       | GPIO7        | SPI MOSI |
| NSS        | GPIO10       | SPI CS   |
| RST        | GPIO9        | Reset    |
| DIO0       | GPIO3        | IRQ      |

---

## Gateway - ESP32-S3

### LoRa Module (SX1276) - FSPI
| SX1276 Pin | ESP32-S3 Pin | Σημείωση |
|------------|--------------|----------|
| VCC        | 3.3V         |          |
| GND        | GND          |          |
| SCK        | GPIO4        | FSPI CLK |
| MISO       | GPIO5        | FSPI MISO|
| MOSI       | GPIO6        | FSPI MOSI|
| NSS        | GPIO7        | FSPI CS  |
| RST        | GPIO15       | Reset    |
| DIO0       | GPIO16       | IRQ      |

### TFT Display (ST7789 320x240) - HSPI
| TFT Pin    | ESP32-S3 Pin | Σημείωση |
|------------|--------------|----------|
| SCLK       | GPIO9        | HSPI CLK |
| MOSI       | GPIO10       | HSPI MOSI|
| CS         | GPIO13       | Chip Select |
| DC         | GPIO12       | Data/Command |
| RST        | GPIO11       | Reset    |
| BL         | GPIO41       | Backlight (timeout: 20s) |

### User Interface
| Λειτουργία | ESP32-S3 Pin | Σημείωση |
|------------|--------------|----------|
| Button     | GPIO0        | Long press: 3s |

---

## I2C Addresses (Sensor Node)

| Συσκευή | Address |
|---------|---------|
| BME680  | 0x77    |
| SCD40   | 0x62    |

---

## Configuration Defaults

### Sensor Node
| Παράμετρος | Τιμή | Σημείωση |
|------------|------|----------|
| Node ID    | 1    |          |
| Sleep Time | 300s | 5 λεπτά  |
| Warmup Time| 25s  |          |
| LoRa Profile| 2   |          |
| AES Key    | "MySecretKey12345" | 16 bytes |

### Gateway
| Παράμετρος | Τιμή | Σημείωση |
|------------|------|----------|
| MQTT Server| d0a16fcc.ala.eu-central-1.emqxsl.com | EMQX Cloud |
| MQTT Port  | 8883 | TLS      |
| MQTT Topic | lora/sensors |     |
| LoRa Profile| 3   | SF10 Long Range |
| Hostname   | s3gateway |        |
| NTP Server | pool.ntp.org |     |
| Timezone   | GMT+2 (+DST) | Ελλάδα |

---

## Σημειώσεις

1. **ESP32-C3**: Χρησιμοποιεί P-MOS για power control των αισθητήρων (GPIO8)
2. **ESP32-S3**: Διπλό SPI bus - FSPI για LoRa, HSPI για TFT
3. **SX1276**: Λειτουργεί στα 3.3V - ΜΗΝ συνδέσετε σε 5V
4. **ZH03B**: Χρειάζεται 5V από boost converter (ελέγχεται από GPIO5)
5. **TFT Backlight**: Αυτόματο timeout μετά από 20s αδράνειας
6. **Magic Number**: 0xCAFE για validation των settings στη μνήμη
