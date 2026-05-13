// SX1262 RX-only wrapper for the Heltec V4. Uses RadioLib + DIO1 ISR so the
// loop just polls poll() instead of busy-waiting.
#pragma once

#include <stdint.h>

struct LoraRxFrame {
    uint8_t  bytes[64];
    uint8_t  len;
    int16_t  rssi_dbm;   // ×1
    int16_t  snr_x10;    // SNR in 0.1 dB steps (e.g. 95 = 9.5 dB)
};

class LoraRadio {
public:
    bool begin();

    // Returns true and fills `out` if a frame arrived since the last call.
    // Re-arms the radio for the next packet automatically.
    bool poll(LoraRxFrame* out);
};
