// LoRa pressure receiver — Heltec WiFi LoRa 32 V4.
// Listens continuously for LoraPressurePacket frames from the RAK4631 sensor
// node, validates them, updates the OLED, and logs each one to serial.

#include <Arduino.h>

#include "config.h"
#include "packet.h"
#include "lora_radio.h"
#include "display.h"
#include "metrics.h"

static LoraRadio lora;
static Display   disp;

static uint32_t s_last_tick_ms = 0;
static uint32_t s_packets_ok   = 0;
static uint32_t s_packets_bad  = 0;

static void blink_led(uint16_t ms) {
    digitalWrite(HELTEC_LED, HIGH);
    delay(ms);
    digitalWrite(HELTEC_LED, LOW);
}

static bool decode(const LoraRxFrame& frame, LoraPressurePacket* out) {
    if (frame.len != sizeof(LoraPressurePacket)) return false;
    memcpy(out, frame.bytes, sizeof(*out));
    if (out->magic   != LORA_PKT_MAGIC)   return false;
    if (out->version != LORA_PKT_VERSION) return false;
    uint16_t expected = lora_crc16((const uint8_t*)out,
                                   sizeof(*out) - sizeof(out->crc16));
    return expected == out->crc16;
}

static void log_packet(const LoraPressurePacket& p,
                       int16_t rssi, int16_t snr10) {
    Serial.printf(
        "[rx] node=%u seq=%u psi=%d.%02d temp=%d.%dC batt=%umV "
        "rssi=%d snr=%d.%d flags=0x%02x ok=%lu bad=%lu\n",
        p.node_id, p.seq,
        p.pressure_cpsi / 100, abs(p.pressure_cpsi % 100),
        p.temp_cdegc / 10, abs(p.temp_cdegc % 10),
        p.batt_mv,
        rssi, snr10 / 10, abs(snr10 % 10),
        p.flags,
        (unsigned long)s_packets_ok, (unsigned long)s_packets_bad);
}

void setup() {
    Serial.begin(115200);
    uint32_t deadline = millis() + 2000;
    while (!Serial && (int32_t)(deadline - millis()) > 0) { delay(10); }

    pinMode(HELTEC_LED, OUTPUT);
    digitalWrite(HELTEC_LED, LOW);

    if (!disp.begin())  Serial.println("[boot] OLED init FAILED");
    if (!lora.begin())  Serial.println("[boot] LoRa init FAILED");
    Serial.printf("[boot] freq=%lu Hz SF%d BW%dk CR4/%d sync=0x%02x\n",
                  (unsigned long)LORA_FREQ_HZ, LORA_SF, LORA_BW_KHZ,
                  LORA_CR, LORA_SYNC_PRIVATE);

    if (!metrics::begin()) Serial.println("[boot] WiFi init FAILED — will retry on next push");

    disp.show_listening();
    blink_led(50);
}

void loop() {
    LoraRxFrame frame;
    if (lora.poll(&frame)) {
        LoraPressurePacket pkt;
        if (decode(frame, &pkt)) {
            s_packets_ok++;
            disp.on_packet(pkt, frame.rssi_dbm, frame.snr_x10, millis());
            metrics::on_rx(pkt, frame.rssi_dbm, frame.snr_x10);
            log_packet(pkt, frame.rssi_dbm, frame.snr_x10);
            blink_led(20);
        } else {
            s_packets_bad++;
            metrics::on_drop();
            Serial.printf("[rx] dropped len=%u rssi=%d snr=%d.%d "
                          "(magic/crc/version mismatch)\n",
                          frame.len, frame.rssi_dbm,
                          frame.snr_x10 / 10, abs(frame.snr_x10 % 10));
        }
    }

    uint32_t now = millis();
    if (now - s_last_tick_ms >= DISPLAY_REFRESH_MS) {
        s_last_tick_ms = now;
        disp.tick(now);
    }
    if (metrics::push_due(now)) {
        metrics::push();
    }
}
