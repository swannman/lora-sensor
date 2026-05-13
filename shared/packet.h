// Binary packet exchanged between sensor nodes and the receiver.
// Shared between sensor-node/ and (future) receiver-node/ — keep ABI stable.
#pragma once

#include <stdint.h>

#define LORA_PKT_MAGIC   0xA517
#define LORA_PKT_VERSION 1

enum : uint8_t {
    PKT_FLAG_HEARTBEAT    = 1 << 0,  // periodic ping (also set on first TX after boot)
    PKT_FLAG_SENSOR_FAULT = 1 << 1,
    PKT_FLAG_STALE_DATA   = 1 << 2,
    PKT_FLAG_LOW_BATT     = 1 << 3,  // batt_mv < LOW_BATT_MV (sensor self-throttling)
    PKT_FLAG_EVENT        = 1 << 4,  // pressure delta crossed THRESHOLD_PSI_X100
};

#pragma pack(push, 1)
struct LoraPressurePacket {
    uint16_t magic;          // LORA_PKT_MAGIC — quick filter on RX
    uint8_t  version;        // LORA_PKT_VERSION
    uint8_t  node_id;        // sender id, 1..255
    uint16_t seq;            // wraps; helps detect loss / replay
    int16_t  pressure_cpsi;  // pressure × 100 (centi-PSI). 0..10000 = 0..100 PSI
    int16_t  temp_cdegc;     // temperature × 10 (deci-°C)
    uint16_t batt_mv;        // battery voltage, millivolts
    uint8_t  flags;          // PKT_FLAG_*
    uint8_t  reserved;       // pad to even length; set to 0
    uint16_t crc16;          // CRC16-CCITT over the preceding 14 bytes
};
#pragma pack(pop)

static_assert(sizeof(LoraPressurePacket) == 16, "packet must be 16 bytes");

// CRC16-CCITT, poly 0x1021, init 0xFFFF, no xor-out, MSB-first.
static inline uint16_t lora_crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
