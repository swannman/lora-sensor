// SX1262 wrapper around RadioLib for the RAK4631. TX-only path; RX lives on
// the receiver node.
#pragma once

#include <stdint.h>

class LoraRadio {
public:
    // Configures pins (from the wiscore_rak4631 board variant), TCXO, RF
    // switch routing, and the modulation params from config.h. Returns true
    // on success.
    bool begin();

    // Transmits a payload synchronously. `len` must be <= 255. Returns true
    // if RadioLib reported success. Leaves the radio in sleep on completion.
    bool transmit(const uint8_t* data, uint8_t len);

    // Force the radio into the lowest sleep state (cold start required to
    // wake). Called automatically after transmit().
    void sleep();
};
