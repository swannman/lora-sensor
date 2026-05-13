// LoRa pressure sensor node — RAK4631 + M32JM-000105-100PG.
// Sleeps between samples; transmits on threshold crossing or heartbeat.

#include <Arduino.h>

#include "config.h"
#include "packet.h"
#include "m3200.h"
#include "lora_radio.h"
#include "power.h"

// Sensor wiring on the RAK19007 base-board breakout pads:
//   V+ (RED)  → IO1   (P0.17, gated by firmware via H0H1 high-drive)
//   GND (BLK) → GND
//   SDA (GRN) → SDA   (P0.13)
//   SCL (WHT) → SCL   (P0.14)
// I²C is bit-banged inside m3200.cpp — we do NOT use Wire — because the
// M3200 forbids repeated-START (datasheet §1.8) and Adafruit's nRF52
// Wire/TWIM emits Sr in subtle ways that lock the sensor up.
static M3200      sensor(WB_I2C1_SDA, WB_I2C1_SCL, WB_IO1);
static LoraRadio  lora;

static uint16_t s_seq = 0;
static int16_t  s_last_tx_cpsi = INT16_MIN;          // INT16_MIN = "no TX yet"
static uint32_t s_last_tx_s    = 0;
static bool     s_lora_ok      = false;

static void blink(uint8_t pin, uint8_t times, uint16_t period_ms) {
    for (uint8_t i = 0; i < times; ++i) {
        digitalWrite(pin, HIGH);
        delay(period_ms / 2);
        digitalWrite(pin, LOW);
        delay(period_ms / 2);
    }
}

static void build_packet(LoraPressurePacket* pkt,
                         const M3200Reading& r,
                         uint16_t batt_mv,
                         bool heartbeat,
                         bool event) {
    pkt->magic         = LORA_PKT_MAGIC;
    pkt->version       = LORA_PKT_VERSION;
    pkt->node_id       = NODE_ID;
    pkt->seq           = s_seq++;
    pkt->pressure_cpsi = r.pressure_cpsi;
    pkt->temp_cdegc    = r.temp_cdegc;
    pkt->batt_mv       = batt_mv;
    pkt->flags         = 0;
    pkt->reserved      = 0;
    if (heartbeat)                  pkt->flags |= PKT_FLAG_HEARTBEAT;
    if (event)                      pkt->flags |= PKT_FLAG_EVENT;
    if (r.status == M3200_STALE)    pkt->flags |= PKT_FLAG_STALE_DATA;
    if (r.status == M3200_FAULT ||
        r.status == M3200_CMD)      pkt->flags |= PKT_FLAG_SENSOR_FAULT;
    if (batt_mv < LOW_BATT_MV)      pkt->flags |= PKT_FLAG_LOW_BATT;
    pkt->crc16 = lora_crc16((const uint8_t*)pkt, sizeof(*pkt) - sizeof(pkt->crc16));
}

static void transmit(const LoraPressurePacket& pkt) {
    digitalWrite(LED_BLUE, HIGH);
    bool ok = lora.transmit((const uint8_t*)&pkt, sizeof(pkt));
    digitalWrite(LED_BLUE, LOW);
    Serial.printf(ok ? "[tx] seq=%u flags=0x%02x bytes=%u\n"
                     : "[tx] FAILED seq=%u\n",
                  (unsigned)pkt.seq, (unsigned)pkt.flags, (unsigned)sizeof(pkt));
}

void setup() {
    Serial.begin(115200);
    // Wait briefly for the host to attach during dev — won't matter when
    // running off battery with no USB host.
    uint32_t deadline = millis() + 2000;
    while (!Serial && (int32_t)(deadline - millis()) > 0) { delay(10); }

    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE,  OUTPUT);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_BLUE,  LOW);

    sensor.begin();   // configures power pin (H0H1), bit-bang SCL/SDA pins, warm-up
    power_begin();

    s_lora_ok = lora.begin();
    Serial.printf("[boot] node=%u lora=%s\n", (unsigned)NODE_ID, s_lora_ok ? "ok" : "FAIL");
    if (s_lora_ok) blink(LED_GREEN, 2, 200);
    else           blink(LED_BLUE,  6, 100);
}

void loop() {
    uint32_t now_s = millis() / 1000;

    // Battery is checked first because the cheapest way to protect a LiPo
    // is to not turn the rest of the radio on at all when it's depleted.
    uint16_t batt_mv = read_battery_mv();

    if (batt_mv < CRIT_BATT_MV) {
        // One last "I'm dying" packet on entry, then long-sleep until the
        // battery either recovers (recharge) or the PCM cuts everything.
        static bool s_crit_announced = false;
        if (!s_crit_announced && s_lora_ok) {
            M3200Reading r = sensor.sample();
            LoraPressurePacket pkt;
            build_packet(&pkt, r, batt_mv, /*heartbeat=*/true, /*event=*/false);
            transmit(pkt);
            s_crit_announced = true;
        }
        Serial.printf("[crit-batt] %u mV — sleeping %us\n",
                      (unsigned)batt_mv, (unsigned)CRIT_SLEEP_S);
        // Re-arm the announcement flag if we ever recover so a future drop
        // gets one fresh "dying" notification rather than silent re-entry.
        if (batt_mv >= LOW_BATT_MV) s_crit_announced = false;
        deep_sleep_ms((uint32_t)CRIT_SLEEP_S * 1000UL);
        return;
    }

    M3200Reading r = sensor.sample();
    Serial.printf("[sample] t=%lus status=%u p=%d cpsi t=%d cdegC batt=%u mV%s\n",
                  (unsigned long)now_s, (unsigned)r.status,
                  (int)r.pressure_cpsi, (int)r.temp_cdegc, (unsigned)batt_mv,
                  batt_mv < LOW_BATT_MV ? " LOW" : "");

    bool low_batt        = batt_mv < LOW_BATT_MV;
    bool first_tx        = (s_last_tx_cpsi == INT16_MIN);
    bool heartbeat_due   = (now_s - s_last_tx_s) >= (uint32_t)HEARTBEAT_INTERVAL_S;
    int32_t delta        = (int32_t)r.pressure_cpsi - (int32_t)s_last_tx_cpsi;
    if (delta < 0) delta = -delta;
    bool threshold_hit   = !first_tx && !low_batt && (delta >= THRESHOLD_PSI_X100);
    bool cooldown_ok     = first_tx || (now_s - s_last_tx_s) >= (uint32_t)MIN_TX_COOLDOWN_S;
    bool sensor_fault    = (r.status == M3200_FAULT) || (r.status == M3200_CMD);

    // In normal operation, threshold crossings can trigger TX. In low-batt
    // state we throttle to heartbeat-only so the sensor still phones home
    // hourly with a fresh batt_mv reading but stops chasing pressure changes.
    // first_tx fires regardless of sensor health — we want the receiver to
    // know the node is alive (with SENSOR_FAULT flag if applicable) on boot,
    // not wait 5 minutes for the next heartbeat tick.
    bool should_tx = cooldown_ok && s_lora_ok && (
        first_tx ||
        (r.ok && (heartbeat_due || threshold_hit)) ||
        (sensor_fault && heartbeat_due)
    );

    if (should_tx) {
        LoraPressurePacket pkt;
        build_packet(&pkt, r, batt_mv,
                     /*heartbeat=*/heartbeat_due || first_tx,
                     /*event=*/threshold_hit);
        transmit(pkt);
        s_last_tx_s    = now_s;
        s_last_tx_cpsi = r.pressure_cpsi;
    }

    uint32_t sleep_s = low_batt ? (uint32_t)LOW_BATT_SAMPLE_S
                                 : (uint32_t)SAMPLE_INTERVAL_S;
    deep_sleep_ms(sleep_s * 1000UL);
}
