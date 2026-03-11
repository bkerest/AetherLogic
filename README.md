# AetherLogic Firmware

Firmware για το σύστημα παρακολούθησης ποιότητας αέρα και θορύβου σε βιομηχανικά περιβάλλοντα βαφής αλουμινίου.

## Επισκόπηση

Το σύστημα αποτελείται από:
- **Sensor Nodes** (ESP32-C3): Αυτόνομοι κόμβοι με μπαταρία που συλλέγουν δεδομένα από αισθητήρες
- **Gateway** (ESP32-S3): Κεντρική πύλη που λαμβάνει δεδομένα μέσω LoRa και τα προωθεί σε MQTT broker

## Hardware

### Sensor Node (ESP32-C3)
| Αισθητήρας | Μέτρηση |
|------------|---------|
| BME680 | Θερμοκρασία, Υγρασία, Πίεση, VOC |
| SCD40 | CO₂ |
| ZH03B | Σωματίδια (PM2.5, PM10) |
| INMP441 | Θόρυβος (I2S) |

### Gateway (ESP32-S3)
- SX1276 LoRa module
- WiFi σύνδεση με MQTT broker

### LoRa Configuration
- Συχνότητα: 868 MHz
- Spreading Factor: SF10
- Bandwidth: 125 kHz

## Δομή Repository

```
aetherlogic/firmware/
├── sensor-node/          # ESP32-C3 firmware
│   └── sensor-node.ino
├── gateway/              # ESP32-S3 firmware
│   └── gateway.ino
└── docs/
    └── pinouts.md
    └──docs/gerber/
        └── Gerber_GateWay.zip        # Σχέδια πλακέτας Πύλης
        └── Gerber_Node.zip           # Σχέδια πλακέτας αισθητήρων
        └── Gerber_Node_Power.zip     # Σχέδια πλακέτας τροφοδοσίας αισθητήρων
    └──docs/grafana/
        └── AetherLogic - Main Dashboard-1773261228574.json     # Κυρίως Πίνακας ελέγχου με κάτοψη εργοστασίου
        └── AetherLogic - Nodes Dashboard-1773261255099.json    # Πίνακας ελέγχου ανά κόμβο
        └── alert-rules-1773261341571.json                      # Κανόνες ειδοποιήσεων
        └── contact-points-1773261295272.json                   # Σημεία επαφής
        └── elvial_floorplan.svg                                # Κάτοψη εργοστασίου σε μορφή svg
        └── JS_Initialization.js                                # Αρχείο αρχικοποίησης Main Dashboard
        └── JS_Render_Code.js                                   # Αρχείο λειτουργίας js/react Main Dashboard
    └──docs/influx/
        └── influxdb-schema.md    # Λειτουργικά ερωτήματα Influx και downsampling
    └──docs/monitoring/
        └── docker-compose.yml    # Αρχείο ρυθμίσων docker station
    └──docs/stl/
        └── GATEWAY_BOTTOM.stl    # Εκτυπώσιμο (3D) περιβλήματος πύλης
        └── GATEWAY_CAP.stl       # Εκτυπώσιμο (3D) καπακιού πύλης
        └── NODE_BOTTOM.stl       # Εκτυπώσιμο (3D) περιβλήματος κόμβου
        └── NODE_CAP.stl          # Εκτυπώσιμο (3D) καπακιού κόμβου
        └── PCB_HOLDER.stl        # Εκτυπώσιμο (3D) βάσης στήριξης PCB
    └──docs/telegraf/
        └── telegraf.conf         # Αρχείο ρυθμίσεων telegraf
```

## Απαιτήσεις

- Arduino IDE 2.x
- ESP32 Board Package (by Espressif)
- Βιβλιοθήκες:
  - RadioLib (LoRa)
  - Adafruit BME680
  - Sensirion SCD4x
  - PubSubClient (MQTT)

## Εγκατάσταση

1. Άνοιξε το Arduino IDE
2. Εγκατάστησε το ESP32 board package:
   - File → Preferences → Additional Board Manager URLs:
   - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Tools → Board → ESP32 → επίλεξε το αντίστοιχο board
4. Άνοιξε τον φάκελο `sensor-node/` ή `gateway/` ανάλογα

## Backend

Τα δεδομένα αποστέλλονται σε MQTT broker (EMQX) και αποθηκεύονται σε InfluxDB μέσω Telegraf. Visualization με Grafana.

Dashboard: https://aetherlogic.eu/

## Άδεια

Μέρος πτυχιακής εργασίας - ΕΑΠ - ΠΛΗ40

## Συγγραφέας

Βασίλειος Κερεστετζής - Υπό την επίβλεψη του Καθηγητή Μηνά Δασυγένη