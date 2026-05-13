# lora-sensor

A long-range water-pressure telemetry rig for outdoor irrigation: a battery-powered LoRa sensor node reads a TE M32JM transducer and transmits to a WiFi-connected receiver that pushes metrics to Grafana Cloud.

```
[M32JM transducer] -- I²C --> [RAK4631 sensor node] --LoRa P2P--> [Heltec V4 receiver] --WiFi/OTLP--> [Grafana Cloud]
                              (battery-powered,                    (mains-powered,
                               solar enclosure)                     OLED display)
```

## Hardware

### Sensor node

| Part | Notes |
| --- | --- |
| **RAK4631** WisBlock Core | nRF52840 MCU + SX1262 LoRa radio in one module |
| **RAK13002** IO Adapter   | Breaks WB_IO1, I²C, GND, and 3V3 out to a screw terminal block |
| **RAK Solar Unify Enclosure** | Outdoor IP65 housing with integrated solar panel + LiPo + battery management |
| **TE M32JM-000105-100PG** | 0–100 PSI gauge pressure transducer, M8-4 connector, I²C interface |
| M8-4 to flying-leads cable | 4-conductor shielded; red=V+, black=GND, white=SCL, green=SDA |
| 1/4″ NPT brass tee | Splices the transducer into the irrigation manifold downstream of the FEBCO backflow preventer |

**Wiring** (sensor → RAK13002 terminals):

| M32JM | RAK pin | Notes |
| --- | --- | --- |
| Red (V+)   | `WB_IO1` | Gated 3.3 V supply. `WB_IO1` is configured in **H0H1 high-drive mode** so it can source the sensor's 3.3 mA cleanly |
| Black (GND)| `GND`    | |
| White (SCL)| `WB_I2C1_SCL` | |
| Green (SDA)| `WB_I2C1_SDA` | |

The shield braid is left floating at the sensor end and tied to GND at the board end.

**Sensor-node quirks worth knowing**:
- The RAK4631's stock Adafruit BSP wires the default `SPI` to IO_SLOT pins (P0.03/29/30), not the SX1262 pins (P1.10–15). `lora_radio.cpp` calls `SPI.setPins(PIN_LORA_MISO, PIN_LORA_SCK, PIN_LORA_MOSI)` before `SPI.begin()` to redirect.
- `PIN_LORA_ANT_PWR` (P1.05 / pin 37) must be driven HIGH to enable the SX1262's antenna switch. Forgetting this gives a clean `RADIOLIB_ERR_CHIP_NOT_FOUND` at init.
- The M32JM's I²C interface forbids repeated-START (datasheet §1.8); Adafruit's `Wire`/TWIM emits Sr in subtle ways that lock the sensor up returning STALE+zeros. The driver in `m3200.cpp` is a verbatim port of the datasheet's bit-banged sample code.
- `M3200_ZERO_OFFSET_CPSI` is hardcoded per-sensor (currently `-25`, i.e. +0.25 PSI of zero correction). Re-measure if you swap the transducer.

### Receiver node

| Part | Notes |
| --- | --- |
| **Heltec WiFi LoRa 32 V4** | ESP32-S3 + SX1262 + 0.96″ SSD1306 OLED on one board. Pinout matches V3, so PlatformIO uses `board = heltec_wifi_lora_32_V3`. |
| 868/915 MHz LoRa whip antenna | The bundled antenna; range is fine indoors with the sensor 30 m away through a wall + 15 m of yard. |
| USB-C power | Mains-powered; sits indoors on WiFi. A 90° USB-C cable helps if the board is wedged into a tight spot. |

### Power

- Sensor: 3.7 V LiPo (charged by the Solar Unify panel) → RAK4631's onboard regulator. Battery readback through `WB_A0` and the onboard 1.5 MΩ / 1.0 MΩ divider; calibrated against a known supply (see `power.cpp`).
- Receiver: USB-C, no battery.

## Component summary

| Role     | Board                              | Radio              | Sensor / Display              |
| -------- | ---------------------------------- | ------------------ | ----------------------------- |
| Sensor   | RAK4631 (nRF52840) in Solar Unify  | SX1262 @ 22 dBm    | TE M32JM-000105-100PG (0–100 PSI, I²C) |
| Receiver | Heltec WiFi LoRa 32 V4 (ESP32-S3)  | SX1262 @ RX-only   | 0.96″ SSD1306 OLED            |

## LoRa link

- 917 MHz · BW 125 kHz · SF11 · CR 4/8 · sync 0x12 · preamble 12
- 917 MHz sits between Meshtastic US ch28/29 and off the smart-meter cluster — tuned for clean reception at long range
- TX 22 dBm (SX1262 max), ~32 dBm link budget improvement vs. SF7
- Custom binary packet (`shared/packet.h`): 16 bytes, magic + version + CRC16, carries pressure (cpsi), temp (cdegC), battery (mV), seq, and flags

## Sensor-node behavior

- Samples every 15 s; transmits on (a) ≥0.5 PSI delta, (b) 5-min heartbeat, or (c) at boot
- Minimum 10 s between transmissions
- Bit-banged I²C — the M32JM datasheet §1.8 forbids repeated-START, which Adafruit's TWIM driver emits
- Battery protection: hourly heartbeat-only below 3.3 V, long-sleep below 3.0 V
- Flags: `HEARTBEAT`, `SENSOR_FAULT`, `STALE_DATA`, `LOW_BATT`, `EVENT`

## Receiver behavior

- Continuous LoRa RX, validates CRC + magic + version, displays last reading on OLED
- Pushes OTLP HTTP/JSON to Grafana Cloud Prometheus every 30 s
- Metrics: pressure, temperature, battery, RSSI, SNR, last-RX age, per-flag packet counters, last-packet error gauge
- Diagnostic WiFi failure modes: SSID-not-visible vs. auth-failed vs. timeout, with nearby-AP scan dump on failure

## Build & flash

Both nodes use [PlatformIO](https://platformio.org/).

```sh
# Sensor node — RAK4631 over USB DFU
cd sensor-node && pio run -t upload

# Receiver node — ESP32-S3 over USB serial
cd receiver-node && pio run -t upload && pio device monitor
```

## Configuration

Copy the receiver's secrets template and fill in your own values:

```sh
cp receiver-node/include/secrets.h.example receiver-node/include/secrets.h
# edit receiver-node/include/secrets.h
```

You'll need:

- A WiFi SSID + password
- A Grafana Cloud Access Policy token with `metrics:write` scope
- Your Prometheus instance ID (visible at `https://grafana.com/orgs/<org>/stacks`)
- The OTLP endpoint URL for your region

`secrets.h` is gitignored.

Operational tuning (sample interval, heartbeat cadence, delta threshold, LoRa params) lives in each node's `platformio.ini` `build_flags` so it can be overridden per environment without editing source.

## Repository layout

```
shared/         packet.h — binary wire format used by both nodes
sensor-node/    RAK4631 firmware (sampling, TX, battery management)
receiver-node/  Heltec V4 firmware (RX, OLED, Grafana push)
```
