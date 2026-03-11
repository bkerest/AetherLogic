# InfluxDB Schema & Data Pipeline - AetherLogic IoT System

## Περιεχόμενα
1. [Γενική Επισκόπηση](#γενική-επισκόπηση)
2. [Buckets & Retention Policies](#buckets--retention-policies)
3. [Measurements & Schema](#measurements--schema)
4. [Downsampling Tasks](#downsampling-tasks)
5. [Queries & Aggregations](#queries--aggregations)
6. [Storage Calculations](#storage-calculations)

---

## Γενική Επισκόπηση

Το σύστημα AetherLogic χρησιμοποιεί **InfluxDB v2** ως time-series database για την αποθήκευση δεδομένων αισθητήρων και gateway health metrics.

### Ροή Δεδομένων

```
┌─────────────┐     LoRa      ┌─────────┐    MQTT     ┌────────────┐
│ Sensor Node │ ──────────────>│ Gateway │ ──────────> │ EMQX Broker│
└─────────────┘   868MHz       └─────────┘   TLS:8883  └────────────┘
                                                              │
                                                              │ MQTT
                                                              ▼
                                                        ┌──────────┐
                                                        │ Telegraf │
                                                        └──────────┘
                                                              │
                                                              │ Line Protocol
                                                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                          InfluxDB v2                                │
├─────────────────────────────────────────────────────────────────────┤
│  Bucket: eap_plh40       (90 days)   ← Raw 5min data               │
│  Bucket: eap_plh40_1h    (1 year)    ← Hourly aggregates           │
│  Bucket: eap_plh40_1d    (5 years)   ← Daily aggregates            │
└─────────────────────────────────────────────────────────────────────┘
                                 │
                                 │ Flux Queries
                                 ▼
                           ┌──────────┐
                           │ Grafana  │
                           └──────────┘
```

---

## Buckets & Retention Policies

### Σχεδιασμός Buckets

Το σύστημα χρησιμοποιεί **3-tier retention strategy** για βέλτιστη χρήση αποθηκευτικού χώρου:

| Bucket | Retention | Data Interval | Purpose | Requirement |
|--------|-----------|---------------|---------|-------------|
| `eap_plh40` | 90 days | 5 minutes | Recent detailed data | FR-STOR-02 (partial) |
| `eap_plh40_1h` | 1 year | 1 hour avg | Historical trends | FR-STOR-02 (≥1 year) ✅ |
| `eap_plh40_1d` | 5 years | 1 day avg | Long-term analysis | FR-STOR-04 ✅ |

### Δημιουργία Buckets (InfluxDB CLI)

```bash
# Bucket 1: Raw data (90 days)
influx bucket create \
  --name eap_plh40 \
  --org "Hellenic Open University" \
  --retention 90d

# Bucket 2: Hourly aggregates (1 year)
influx bucket create \
  --name eap_plh40_1h \
  --org "Hellenic Open University" \
  --retention 365d

# Bucket 3: Daily aggregates (5 years)
influx bucket create \
  --name eap_plh40_1d \
  --org "Hellenic Open University" \
  --retention 1825d
```

### Δημιουργία Buckets (InfluxDB UI)

1. Ανοίξτε InfluxDB UI: `http://192.168.2.5:8086`
2. Πλοηγηθείτε: **Data** → **Buckets**
3. Κάντε κλικ: **+ Create Bucket**
4. Συμπληρώστε:
   - **Name**: `eap_plh40`
   - **Delete Data**: **Older than 90 days**
   - **Organization**: `Hellenic Open University`
5. Επαναλάβετε για `eap_plh40_1h` (365d) και `eap_plh40_1d` (1825d)

---

## Measurements & Schema

### Measurement 1: `air_quality`

Δεδομένα αισθητήρων από LoRa nodes.

#### Tags (Indexed)

| Tag | Type | Description | Example |
|-----|------|-------------|---------|
| `id` | string | Node ID (από device configuration) | "1", "2", "3" |
| `gw` | string | Gateway ID (ESP32-S3 MAC-based) | "ESP32S3ABC123" |
| `node` | string | Friendly node name | "node_01", "FactoryNode_01" |

#### Fields (Not Indexed)

| Field | Type | Unit | Description | Range |
|-------|------|------|-------------|-------|
| `t` | float | °C | Temperature | -40 to 85 |
| `h` | float | % | Relative Humidity | 0 to 100 |
| `p` | float | hPa | Atmospheric Pressure | 300 to 1100 |
| `alt` | float | m | Calculated Altitude | -500 to 9000 |
| `iaq` | float | - | Indoor Air Quality Index | 0 to 500 |
| `tvoc` | float | kΩ | Gas Resistance (BME680) | 0 to 500 |
| `co2` | integer | ppm | CO₂ Concentration (SCD40) | 400 to 5000 |
| `pm1` | integer | µg/m³ | PM1.0 Particulate Matter | 0 to 1000 |
| `pm25` | integer | µg/m³ | PM2.5 Particulate Matter | 0 to 1000 |
| `pm10` | integer | µg/m³ | PM10 Particulate Matter | 0 to 1000 |
| `noise` | float | dB SPL | Noise Level | 30 to 130 |
| `batt` | float | V | Battery Voltage | 2.8 to 4.2 |
| `rssi` | integer | dBm | LoRa Signal Strength | -120 to -30 |

#### Example Line Protocol

```
air_quality,id=1,gw=ESP32S3ABC123,node=node_01 t=24.5,h=45.2,p=1013.25,alt=120.5,iaq=125,tvoc=45.23,co2=450,pm1=8,pm25=12,pm10=25,noise=65.3,batt=3.82,rssi=-85 1706097045000000000
```

#### Example JSON Payload (from MQTT)

```json
{
  "timestamp": "2024-01-24T14:30:45+02:00",
  "gw": "ESP32S3ABC123",
  "id": 1,
  "node": "node_01",
  "t": 24.5,
  "h": 45.2,
  "p": 1013.25,
  "alt": 120.5,
  "iaq": 125,
  "tvoc": 45.23,
  "co2": 450,
  "pm1": 8,
  "pm25": 12,
  "pm10": 25,
  "noise": 65.3,
  "batt": 3.82,
  "rssi": -85
}
```

---

### Measurement 2: `gateway_health`

Gateway heartbeat και health metrics (κάθε 5 λεπτά).

#### Tags

| Tag | Type | Description | Example |
|-----|------|-------------|---------|
| `gw` | string | Gateway ID | "ESP32S3ABC123" |
| `type` | string | Message type | "heartbeat" |
| `fw` | string | Firmware version | "2.5.2" |

#### Fields

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `uptime` | integer | seconds | Time since boot |
| `heap` | integer | bytes | Free heap memory |
| `wifi_rssi` | integer | dBm | WiFi signal strength |
| `lora_packets` | integer | count | Total LoRa packets received |
| `buffer` | integer | count | Buffered MQTT messages |
| `ntp_synced` | boolean | - | NTP synchronization status |
| `lora_ok` | boolean | - | LoRa module initialization status |

#### Example Line Protocol

```
gateway_health,gw=ESP32S3ABC123,type=heartbeat,fw=2.5.2 uptime=86400,heap=245760,wifi_rssi=-65,lora_packets=1440,buffer=0,ntp_synced=true,lora_ok=true 1706097045000000000
```

---

## Downsampling Tasks

Τα **InfluxDB Tasks** εκτελούν αυτόματα aggregations για downsampling.

### Task 1: Hourly Downsampling

**Σκοπός**: Υπολογισμός ωριαίων μέσων όρων από raw data (5min).

**Συχνότητα**: Κάθε ώρα (στο :05 λεπτό για να έχει ολοκληρωθεί η προηγούμενη ώρα).

**Flux Script**:

```flux
// Task: Downsample to 1-hour averages
option task = {
  name: "downsample_1h_air_quality",
  every: 1h,
  offset: 5m  // Run at :05 past the hour
}

from(bucket: "eap_plh40")
  |> range(start: -2h, stop: -1h)  // Process previous hour
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> aggregateWindow(
      every: 1h,
      fn: mean,
      createEmpty: false
    )
  |> to(
      bucket: "eap_plh40_1h",
      org: "Hellenic Open University"
    )
```

**Δημιουργία Task (InfluxDB UI)**:

1. **Tasks** → **Create Task**
2. **Name**: `downsample_1h_air_quality`
3. **Every**: `1h`
4. **Offset**: `5m`
5. Paste Flux script
6. **Save**

---

### Task 2: Daily Downsampling

**Σκοπός**: Υπολογισμός ημερήσιων μέσων όρων από hourly data.

**Συχνότητα**: Κάθε μέρα στις 00:10.

**Flux Script**:

```flux
// Task: Downsample to 1-day averages
option task = {
  name: "downsample_1d_air_quality",
  every: 1d,
  offset: 10m  // Run at 00:10
}

from(bucket: "eap_plh40_1h")
  |> range(start: -2d, stop: -1d)  // Process previous day
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> aggregateWindow(
      every: 1d,
      fn: mean,
      createEmpty: false
    )
  |> to(
      bucket: "eap_plh40_1d",
      org: "Hellenic Open University"
    )
```

---

### Task 3: Gateway Health Downsampling (Optional)

**Σκοπός**: Υπολογισμός ημερήσιων aggregations για gateway metrics.

```flux
option task = {
  name: "downsample_1d_gateway_health",
  every: 1d,
  offset: 15m
}

from(bucket: "eap_plh40")
  |> range(start: -2d, stop: -1d)
  |> filter(fn: (r) => r._measurement == "gateway_health")
  |> aggregateWindow(
      every: 1d,
      fn: mean,  // or max/min depending on field
      createEmpty: false
    )
  |> to(
      bucket: "eap_plh40_1d",
      org: "Hellenic Open University"
    )
```

---

## Queries & Aggregations

### Basic Queries (Flux)

#### Query 1: Latest Reading από κάθε Node

```flux
from(bucket: "eap_plh40")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "t" or r._field == "h" or r._field == "co2")
  |> last()
  |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
```

#### Query 2: Μέση Θερμοκρασία τελευταίας εβδομάδας

```flux
from(bucket: "eap_plh40_1h")  // Use hourly data for speed
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "t")
  |> mean()
  |> group(columns: ["node"])
```

#### Query 3: PM2.5 Max per Day (Last Month)

```flux
from(bucket: "eap_plh40_1h")
  |> range(start: -30d)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "pm25")
  |> aggregateWindow(every: 1d, fn: max)
```

#### Query 4: Gateway Uptime & Health

```flux
from(bucket: "eap_plh40")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "gateway_health")
  |> filter(fn: (r) => r._field == "uptime" or r._field == "heap")
  |> last()
```

---

### Advanced Aggregations

#### Compute Moving Average (7-day)

```flux
from(bucket: "eap_plh40_1d")
  |> range(start: -90d)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "co2")
  |> movingAverage(n: 7)  // 7-day moving average
```

#### Detect Threshold Violations (PM2.5 > 35 µg/m³)

```flux
from(bucket: "eap_plh40")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "pm25")
  |> filter(fn: (r) => r._value > 35)  // WHO 24h guideline
  |> group(columns: ["node"])
  |> count()
```

#### Correlation Analysis (Temperature vs CO2)

```flux
temp = from(bucket: "eap_plh40_1h")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "t")
  |> drop(columns: ["_measurement"])

co2 = from(bucket: "eap_plh40_1h")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "co2")
  |> drop(columns: ["_measurement"])

join(
  tables: {temp: temp, co2: co2},
  on: ["_time", "node", "gw"]
)
```

---

## Storage Calculations

### Data Volume Estimation

**Assumptions**:
- **Sensor Nodes**: 5
- **Measurements per Node**: 13 fields (t, h, p, alt, iaq, tvoc, co2, pm1, pm25, pm10, noise, batt, rssi)
- **Sampling Interval**: 5 minutes
- **InfluxDB Point Size**: ~8 bytes per field (average)

#### Tier 1: Raw Data (90 days @ 5min)

```
Points per node per day:
  13 fields × (60 min / 5 min) × 24 hours = 13 × 12 × 24 = 3,744 points/day

Points per node for 90 days:
  3,744 × 90 = 336,960 points

Total points (5 nodes):
  336,960 × 5 = 1,684,800 points

Storage (approximate):
  1,684,800 × 8 bytes = 13.5 MB (compressed: ~3-5 MB)
```

#### Tier 2: Hourly Averages (1 year)

```
Points per node per day:
  13 fields × 24 hours = 312 points/day

Points per node for 1 year:
  312 × 365 = 113,880 points

Total points (5 nodes):
  113,880 × 5 = 569,400 points

Storage:
  569,400 × 8 bytes = 4.6 MB (compressed: ~1-2 MB)
```

#### Tier 3: Daily Averages (5 years)

```
Points per node per day:
  13 fields × 1 = 13 points/day

Points per node for 5 years:
  13 × 365 × 5 = 23,725 points

Total points (5 nodes):
  23,725 × 5 = 118,625 points

Storage:
  118,625 × 8 bytes = 0.95 MB (negligible)
```

#### Total Storage

```
Tier 1 (90d raw):     ~3-5 MB
Tier 2 (1y hourly):   ~1-2 MB
Tier 3 (5y daily):    ~1 MB
----------------------------------
TOTAL:                ~5-8 MB (very manageable!)
```

**Συμπέρασμα**: Το σύστημα με 5 sensor nodes καταναλώνει **<10 MB** για 5 χρόνια ιστορικών δεδομένων με downsampling. Αυτό είναι εξαιρετικά αποδοτικό.

---

## Backup & Restore

### Backup Bucket (InfluxDB CLI)

```bash
# Backup all data from bucket
influx backup /backup/path \
  --bucket eap_plh40 \
  --org "Hellenic Open University"

# Backup specific time range
influx backup /backup/path \
  --bucket eap_plh40 \
  --start 2024-01-01T00:00:00Z \
  --stop 2024-12-31T23:59:59Z
```

### Restore Bucket

```bash
influx restore /backup/path \
  --bucket eap_plh40 \
  --org "Hellenic Open University"
```

### Automated Daily Backup (Cron)

```bash
# Add to crontab: crontab -e
0 2 * * * /usr/bin/influx backup /mnt/backups/influxdb/$(date +\%Y\%m\%d) --bucket eap_plh40 --org "Hellenic Open University"
```

---

## Monitoring & Maintenance

### Health Checks

```bash
# Check bucket size
influx bucket list --org "Hellenic Open University"

# Check task status
influx task list --org "Hellenic Open University"

# View task logs
influx task log list --task-id <TASK_ID>

# Check cardinality (unique tag combinations)
influx cardinality --bucket eap_plh40
```

### Performance Tuning

1. **Indexing**: Tags are automatically indexed, fields are not
2. **Cardinality**: Keep tag cardinality low (<100K unique combinations)
3. **Retention**: Aggressive retention policies reduce storage and improve query speed
4. **Downsampling**: Use pre-aggregated buckets for historical queries

---

## Ικανοποίηση Απαιτήσεων

| Requirement ID | Description | Implementation | Status |
|----------------|-------------|----------------|--------|
| FR-STOR-01 | Time-series database | InfluxDB v2 | ✅ |
| FR-STOR-02 | Data retention ≥1 year | `eap_plh40_1h` bucket (365d) | ✅ |
| FR-STOR-03 | Time & node queries | Tags: `id`, `node`, `gw` + timestamp | ✅ |
| FR-STOR-04 | Downsampling | Tasks: 1h, 1d aggregations | ✅ |
| FR-STOR-05 | Automatic deletion | Retention policies (90d, 365d, 1825d) | ✅ |
| FR-STOR-06 | Backup/restore | InfluxDB CLI backup | ✅ (Could Have) |

---

## Πηγές

- [InfluxDB v2 Documentation](https://docs.influxdata.com/influxdb/v2.0/)
- [Flux Language Guide](https://docs.influxdata.com/flux/v0.x/)
- [Telegraf MQTT Consumer](https://github.com/influxdata/telegraf/tree/master/plugins/inputs/mqtt_consumer)
- [Time-Series Data Retention Best Practices](https://www.influxdata.com/blog/tldr-influxdb-tech-tips-august-27-2020/)

---

**Document Version**: 1.0
**Last Updated**: 24 Ιανουαρίου 2026
**Author**: Vasilis Kerestetzis AetherLogic Project - Hellenic Open University
