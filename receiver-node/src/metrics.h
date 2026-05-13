// WiFi + Grafana Cloud OTLP HTTP/JSON metrics push.
// Records each received LoRa packet's last value, then batches a single
// metrics snapshot to Grafana Cloud Prometheus on METRICS_PUSH_INTERVAL_MS.
#pragma once

#include <stdint.h>
#include "packet.h"

namespace metrics {

// Connect WiFi (blocking with timeout), sync NTP. Returns true on success.
// Safe to call even if previous attempt failed; will retry.
bool begin();

// Record a freshly-received valid packet. Stores latest values + bumps RX counter.
void on_rx(const LoraPressurePacket& pkt, int16_t rssi_dbm, int16_t snr_x10);

// Bump the dropped-packet counter (CRC/magic/version mismatch).
void on_drop();

// Returns true if the push interval has elapsed since last_push_ms.
bool push_due(uint32_t now_ms);

// Build OTLP JSON payload and POST it. Non-blocking-ish: returns quickly on
// WiFi failure, otherwise blocks for the duration of the HTTPS POST (~1-3 s
// typical on home WiFi). Logs success/failure to Serial.
void push();

// Status accessor — true once WiFi is connected.
bool wifi_ok();

}  // namespace metrics
