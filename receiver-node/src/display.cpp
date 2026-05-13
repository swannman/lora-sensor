#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "display.h"
#include "config.h"

static Adafruit_SSD1306 oled(128, 64, &Wire, HELTEC_OLED_RESET);

// Latest packet so tick() can re-render without re-receiving.
static LoraPressurePacket s_pkt{};
static int16_t  s_rssi   = 0;
static int16_t  s_snr10  = 0;
static uint32_t s_rx_ms  = 0;
static bool     s_have_data = false;

static void render(uint32_t now_ms) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);

    if (!s_have_data) {
        oled.setCursor(0, 0);
        oled.println(F("LoRa pressure RX"));
        oled.println(F("listening..."));
        oled.print(F("freq: "));
        oled.print(LORA_FREQ_HZ / 1000000UL);
        oled.println(F(" MHz"));
        oled.print(F("SF"));
        oled.print(LORA_SF);
        oled.print(F(" BW"));
        oled.print(LORA_BW_KHZ);
        oled.print(F("k CR4/"));
        oled.println(LORA_CR);
        oled.display();
        return;
    }

    // Pressure — biggest line, gets size-2 font.
    char line[24];
    oled.setTextSize(2);
    oled.setCursor(0, 0);
    // Sign-aware formatting: integer division loses the sign when the whole
    // part is 0 (e.g., -24 cpsi → "0.24"), so we handle the negative case
    // explicitly to make "−0.24 PSI" render correctly.
    int v = s_pkt.pressure_cpsi;
    int whole = v / 100;
    int frac  = abs(v % 100);
    if (v < 0 && whole == 0) snprintf(line, sizeof(line), "-0.%02d PSI", frac);
    else                     snprintf(line, sizeof(line), "%d.%02d PSI", whole, frac);
    oled.println(line);

    oled.setTextSize(1);
    oled.setCursor(0, 18);
    snprintf(line, sizeof(line), "T %d.%d C  Vb %u.%02u",
             s_pkt.temp_cdegc / 10, abs(s_pkt.temp_cdegc % 10),
             s_pkt.batt_mv / 1000, (s_pkt.batt_mv % 1000) / 10);
    oled.println(line);

    snprintf(line, sizeof(line), "Node %u  Seq %u",
             s_pkt.node_id, s_pkt.seq);
    oled.println(line);

    snprintf(line, sizeof(line), "RSSI %d  SNR %d.%d",
             s_rssi, s_snr10 / 10, abs(s_snr10 % 10));
    oled.println(line);

    uint32_t age_s = (now_ms - s_rx_ms) / 1000UL;
    bool stale = age_s >= RX_TIMEOUT_S;
    snprintf(line, sizeof(line), "Age %lus%s", (unsigned long)age_s,
             stale ? " STALE" : "");
    oled.println(line);

    if (s_pkt.flags) {
        oled.print(s_pkt.flags & PKT_FLAG_HEARTBEAT     ? "HB "    : "");
        oled.print(s_pkt.flags & PKT_FLAG_EVENT         ? "EVT "   : "");
        oled.print(s_pkt.flags & PKT_FLAG_STALE_DATA    ? "STALE " : "");
        oled.print(s_pkt.flags & PKT_FLAG_SENSOR_FAULT  ? "FAULT " : "");
        oled.print(s_pkt.flags & PKT_FLAG_LOW_BATT      ? "LOWBAT" : "");
    }

    oled.display();
}

bool Display::begin() {
    // VEXT gates the OLED's 3V3 rail (active LOW). Drive LOW before init or
    // the SSD1306 will NACK on the bus.
    pinMode(HELTEC_VEXT, OUTPUT);
    digitalWrite(HELTEC_VEXT, LOW);
    delay(50);

    Wire.begin(HELTEC_OLED_SDA, HELTEC_OLED_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, HELTEC_OLED_ADDR)) return false;
    oled.clearDisplay();
    oled.display();
    return true;
}

void Display::on_packet(const LoraPressurePacket& pkt,
                        int16_t rssi_dbm, int16_t snr_x10, uint32_t rx_millis) {
    s_pkt    = pkt;
    s_rssi   = rssi_dbm;
    s_snr10  = snr_x10;
    s_rx_ms  = rx_millis;
    s_have_data = true;
    render(rx_millis);
}

void Display::tick(uint32_t now_millis) {
    render(now_millis);
}

void Display::show_listening() {
    s_have_data = false;
    render(millis());
}
