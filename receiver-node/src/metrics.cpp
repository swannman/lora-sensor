#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <ArduinoJson.h>

#include "metrics.h"
#include "config.h"
#include "secrets.h"

namespace metrics {

// --- Latest-value cache (updated on each RX) -------------------------------
static struct {
    bool       have_data       = false;
    float      pressure_psi    = 0.0f;
    float      temp_celsius    = 0.0f;
    uint16_t   batt_mv         = 0;
    int16_t    rssi_dbm        = 0;
    float      snr_db          = 0.0f;
    uint8_t    node_id         = 0;
    uint8_t    flags           = 0;
    uint16_t   last_seq        = 0;
    uint32_t   last_rx_ms      = 0;
    uint32_t   pkt_rx_total          = 0;
    uint32_t   pkt_drop_total        = 0;
    // Per-flag counters — a single packet can increment several of these
    // (e.g., HB+EVT, or HB+FAULT). Use rate() in Grafana to see TX type mix.
    uint32_t   pkt_heartbeat_total   = 0;
    uint32_t   pkt_event_total       = 0;
    uint32_t   pkt_sensor_fault_total = 0;
    uint32_t   pkt_stale_total       = 0;
    uint32_t   pkt_low_batt_total    = 0;
    // Latest-packet "is something wrong" gauge — 1 if any error-class flag
    // (SENSOR_FAULT, STALE, LOW_BATT) was set in the most recent packet.
    uint8_t    last_has_error        = 0;
} s;

static uint32_t s_last_push_ms = 0;
static bool     s_wifi_ok      = false;
static bool     s_ntp_ok       = false;

bool wifi_ok() { return s_wifi_ok; }

bool push_due(uint32_t now_ms) {
    return (now_ms - s_last_push_ms) >= (uint32_t)METRICS_PUSH_INTERVAL_MS;
}

// --- WiFi + NTP -----------------------------------------------------------
static const char* wifi_status_str(wl_status_t s) {
    switch (s) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (SSID not visible)";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED (auth — check password)";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        default:                 return "UNKNOWN";
    }
}

// Scan for nearby SSIDs once, log result, and confirm whether our SSID is
// visible. Helps disambiguate "wrong password" vs "AP out of range / typo".
static bool ssid_visible_in_scan() {
    Serial.println("[wifi] scanning for nearby APs...");
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("[wifi] scan returned 0 networks (radio issue?)");
        return false;
    }
    bool found = false;
    int found_rssi = 0;
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i) == String(WIFI_SSID)) {
            found = true;
            found_rssi = WiFi.RSSI(i);
            break;
        }
    }
    if (found) {
        Serial.printf("[wifi] target SSID '%s' visible (rssi=%d dBm)\n",
                      WIFI_SSID, found_rssi);
    } else {
        Serial.printf("[wifi] target SSID '%s' NOT in scan results — first %d seen:\n",
                      WIFI_SSID, n);
        int show = n < 8 ? n : 8;
        for (int i = 0; i < show; ++i) {
            Serial.printf("        '%s' (rssi=%d)\n",
                          WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
    }
    WiFi.scanDelete();
    return found;
}

static bool connect_wifi() {
    if (WiFi.status() == WL_CONNECTED) { s_wifi_ok = true; return true; }
    Serial.printf("[wifi] connecting to '%s'...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);  // clear previous state so failure code is fresh
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t deadline = millis() + METRICS_WIFI_CONNECT_TIMEOUT_MS;
    wl_status_t last = WL_IDLE_STATUS;
    while (millis() < deadline) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) break;
        // Distinct failure modes: bail early so we can report them.
        if (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED) {
            last = st;
            break;
        }
        last = st;
        delay(200);
    }

    wl_status_t final_status = WiFi.status();
    s_wifi_ok = (final_status == WL_CONNECTED);

    if (s_wifi_ok) {
        Serial.printf("[wifi] connected, ip=%s rssi=%d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.printf("[wifi] connect FAILED — status=%s (last seen %s)\n",
                      wifi_status_str(final_status), wifi_status_str(last));
        // Extra diagnostic: scan and see if the SSID is even reachable.
        // Helps distinguish "wrong SSID/no AP" from "wrong password".
        ssid_visible_in_scan();
    }
    return s_wifi_ok;
}

static bool sync_ntp() {
    if (s_ntp_ok) return true;
    configTime(0, 0, NTP_SERVER);
    Serial.print("[ntp] syncing");
    uint32_t deadline = millis() + 10000;
    time_t now = 0;
    while ((now = time(nullptr)) < 1700000000 && millis() < deadline) {
        Serial.print(".");
        delay(250);
    }
    Serial.println();
    s_ntp_ok = (now >= 1700000000);
    if (s_ntp_ok) Serial.printf("[ntp] synced (epoch=%ld)\n", (long)now);
    else          Serial.println("[ntp] sync failed");
    return s_ntp_ok;
}

bool begin() {
    if (!connect_wifi()) return false;
    sync_ntp();
    return s_wifi_ok;
}

// --- Packet sinks ---------------------------------------------------------
void on_rx(const LoraPressurePacket& pkt, int16_t rssi_dbm, int16_t snr_x10) {
    s.have_data    = true;
    s.pressure_psi = pkt.pressure_cpsi / 100.0f;
    s.temp_celsius = pkt.temp_cdegc / 10.0f;
    s.batt_mv      = pkt.batt_mv;
    s.rssi_dbm     = rssi_dbm;
    s.snr_db       = snr_x10 / 10.0f;
    s.node_id      = pkt.node_id;
    s.flags        = pkt.flags;
    s.last_seq     = pkt.seq;
    s.last_rx_ms   = millis();
    s.pkt_rx_total++;

    if (pkt.flags & PKT_FLAG_HEARTBEAT)    s.pkt_heartbeat_total++;
    if (pkt.flags & PKT_FLAG_EVENT)        s.pkt_event_total++;
    if (pkt.flags & PKT_FLAG_SENSOR_FAULT) s.pkt_sensor_fault_total++;
    if (pkt.flags & PKT_FLAG_STALE_DATA)   s.pkt_stale_total++;
    if (pkt.flags & PKT_FLAG_LOW_BATT)     s.pkt_low_batt_total++;

    const uint8_t ERROR_MASK = PKT_FLAG_SENSOR_FAULT | PKT_FLAG_STALE_DATA | PKT_FLAG_LOW_BATT;
    s.last_has_error = (pkt.flags & ERROR_MASK) ? 1 : 0;
}

void on_drop() { s.pkt_drop_total++; }

// --- OTLP push ------------------------------------------------------------
// Builds one OTLP/HTTP/JSON document with a gauge dataPoint per metric, all
// sharing the same resource attributes. The resource attributes set
// service.name, host.name, and node_id — these become labels in Mimir.
static void build_payload(String& out) {
    JsonDocument doc;

    // Timestamp in nanoseconds — OTLP requires a string for int64 to avoid
    // precision loss in JS-style parsers.
    int64_t now_ns = (int64_t)time(nullptr) * 1000000000LL;
    char ts_buf[24];
    snprintf(ts_buf, sizeof(ts_buf), "%lld", (long long)now_ns);

    JsonObject rm = doc["resourceMetrics"].add<JsonObject>();
    JsonObject resource = rm["resource"].to<JsonObject>();
    JsonArray attrs = resource["attributes"].to<JsonArray>();

    auto add_attr = [&](const char* k, const char* v) {
        JsonObject a = attrs.add<JsonObject>();
        a["key"] = k;
        a["value"]["stringValue"] = v;
    };
    add_attr("service.name", METRICS_SERVICE_NAME);
    add_attr("host.name",    METRICS_HOST_NAME);
    char node_id_buf[8];
    snprintf(node_id_buf, sizeof(node_id_buf), "%u", (unsigned)s.node_id);
    add_attr("node_id", node_id_buf);

    JsonObject sm = rm["scopeMetrics"].add<JsonObject>();
    sm["scope"]["name"] = "lora.sensor";
    JsonArray metrics_arr = sm["metrics"].to<JsonArray>();

    auto add_gauge_double = [&](const char* name, const char* desc, double val) {
        JsonObject m = metrics_arr.add<JsonObject>();
        m["name"] = name;
        m["description"] = desc;
        JsonObject dp = m["gauge"]["dataPoints"].add<JsonObject>();
        dp["asDouble"] = val;
        dp["timeUnixNano"] = ts_buf;
    };
    auto add_sum_int = [&](const char* name, const char* desc, int64_t val) {
        JsonObject m = metrics_arr.add<JsonObject>();
        m["name"] = name;
        m["description"] = desc;
        JsonObject sum = m["sum"].to<JsonObject>();
        sum["aggregationTemporality"] = 2;   // CUMULATIVE
        sum["isMonotonic"] = true;
        JsonObject dp = sum["dataPoints"].add<JsonObject>();
        dp["asInt"] = val;
        dp["timeUnixNano"] = ts_buf;
    };

    if (s.have_data) {
        add_gauge_double("lora_pressure_psi",       "Pressure from sensor in PSI",      s.pressure_psi);
        add_gauge_double("lora_temperature_celsius","Temperature from sensor in °C",   s.temp_celsius);
        add_gauge_double("lora_battery_mv",         "Sensor battery voltage in mV",     (double)s.batt_mv);
        add_gauge_double("lora_rssi_dbm",           "Last RX signal strength in dBm",   (double)s.rssi_dbm);
        add_gauge_double("lora_snr_db",             "Last RX signal-to-noise in dB",    s.snr_db);
        uint32_t age_s = (millis() - s.last_rx_ms) / 1000UL;
        add_gauge_double("lora_last_rx_age_seconds","Seconds since last successful RX", (double)age_s);
        add_gauge_double("lora_last_packet_has_error", "1 if latest packet had any error flag (fault/stale/low_batt)", (double)s.last_has_error);
    }
    add_sum_int("lora_packets_received_total",     "Cumulative packets received with valid CRC", (int64_t)s.pkt_rx_total);
    add_sum_int("lora_packets_dropped_total",      "Cumulative packets dropped (CRC/version mismatch)", (int64_t)s.pkt_drop_total);
    add_sum_int("lora_packets_heartbeat_total",    "Cumulative packets with HEARTBEAT flag set",  (int64_t)s.pkt_heartbeat_total);
    add_sum_int("lora_packets_event_total",        "Cumulative packets with EVENT flag set",      (int64_t)s.pkt_event_total);
    add_sum_int("lora_packets_sensor_fault_total", "Cumulative packets with SENSOR_FAULT flag",   (int64_t)s.pkt_sensor_fault_total);
    add_sum_int("lora_packets_stale_total",        "Cumulative packets with STALE_DATA flag",     (int64_t)s.pkt_stale_total);
    add_sum_int("lora_packets_low_batt_total",     "Cumulative packets with LOW_BATT flag",       (int64_t)s.pkt_low_batt_total);

    serializeJson(doc, out);
}

void push() {
    s_last_push_ms = millis();

    if (!connect_wifi()) return;
    if (!s_ntp_ok && !sync_ntp()) {
        Serial.println("[metrics] skip push — no NTP");
        return;
    }

    String payload;
    payload.reserve(2048);
    build_payload(payload);

    WiFiClientSecure client;
    client.setInsecure();   // skip cert validation — adequate for this hop
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, GRAFANA_OTLP_URL)) {
        Serial.println("[metrics] http.begin FAILED");
        return;
    }
    http.addHeader("Content-Type", "application/json");
    http.setAuthorization(GRAFANA_INSTANCE_ID, GRAFANA_API_TOKEN);

    int code = http.POST(payload);
    if (code >= 200 && code < 300) {
        Serial.printf("[metrics] push ok (HTTP %d, %u bytes)\n",
                      code, (unsigned)payload.length());
    } else {
        String resp = http.getString();
        Serial.printf("[metrics] push FAILED HTTP %d: %s\n",
                      code, resp.substring(0, 200).c_str());
    }
    http.end();
}

}  // namespace metrics
