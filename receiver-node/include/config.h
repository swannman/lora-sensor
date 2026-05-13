// Receiver-side compile-time config. Defaults are set as -D flags in
// platformio.ini; override there.
#pragma once

#include <stdint.h>

// --- Heltec WiFi LoRa 32 V4 pin map (identical to V3) ---------------------
// LoRa SX1262
#define HELTEC_LORA_NSS    8
#define HELTEC_LORA_SCK    9
#define HELTEC_LORA_MOSI   10
#define HELTEC_LORA_MISO   11
#define HELTEC_LORA_RESET  12
#define HELTEC_LORA_BUSY   13
#define HELTEC_LORA_DIO1   14
// OLED SSD1306 over I2C
#define HELTEC_OLED_SDA    17
#define HELTEC_OLED_SCL    18
#define HELTEC_OLED_RESET  21
#define HELTEC_OLED_ADDR   0x3C
// Power switch for OLED + external 3V3 rail. Active-LOW: drive LOW to enable.
#define HELTEC_VEXT        36
// User LED + button
#define HELTEC_LED         35
#define HELTEC_BUTTON      0

// --- LoRa params (must match sensor) --------------------------------------
#ifndef LORA_FREQ_HZ
#define LORA_FREQ_HZ 915000000UL
#endif
#ifndef LORA_BW_KHZ
#define LORA_BW_KHZ 125
#endif
#ifndef LORA_SF
#define LORA_SF 9
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_SYNC_PRIVATE
#define LORA_SYNC_PRIVATE 0x12
#endif
#ifndef LORA_PREAMBLE
#define LORA_PREAMBLE 12
#endif
#define LORA_PREAMBLE_SYMBOLS LORA_PREAMBLE

// --- Receiver behavior -----------------------------------------------------
#ifndef DISPLAY_REFRESH_MS
#define DISPLAY_REFRESH_MS 1000   // refresh "age" counter each second
#endif
#ifndef RX_TIMEOUT_S
#define RX_TIMEOUT_S 300          // mark display stale if no packet for 5 min
#endif

// --- Metrics push (Grafana Cloud via OTLP HTTP/JSON) ----------------------
#ifndef METRICS_PUSH_INTERVAL_MS
#define METRICS_PUSH_INTERVAL_MS 30000   // 30 s — batch interval
#endif
#ifndef METRICS_WIFI_CONNECT_TIMEOUT_MS
#define METRICS_WIFI_CONNECT_TIMEOUT_MS 20000  // 20 s to connect at boot
#endif
#ifndef METRICS_SERVICE_NAME
#define METRICS_SERVICE_NAME "lora-receiver"
#endif
#ifndef METRICS_HOST_NAME
#define METRICS_HOST_NAME "heltec-v4"
#endif
// Time pool — used for OTLP timestamps. Pool.ntp.org is reliable globally.
#ifndef NTP_SERVER
#define NTP_SERVER "pool.ntp.org"
#endif
