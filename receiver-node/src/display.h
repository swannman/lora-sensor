// SSD1306 128x64 wrapper. Layout focuses on a single sensor node; multi-node
// support can be layered on later by keying on packet.node_id.
#pragma once

#include <stdint.h>
#include "packet.h"

class Display {
public:
    bool begin();

    // Replaces the displayed reading with a freshly-decoded packet, including
    // RSSI/SNR from the radio and a wall-clock timestamp (millis()).
    void on_packet(const LoraPressurePacket& pkt,
                   int16_t rssi_dbm, int16_t snr_x10, uint32_t rx_millis);

    // Re-renders just the "age" line; call once per second so the display
    // doesn't lie about freshness between packets.
    void tick(uint32_t now_millis);

    // Splash shown until the first packet arrives.
    void show_listening();
};
