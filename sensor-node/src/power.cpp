#include <Arduino.h>
#include "power.h"

// VBAT divider constants from RAK's reference (1.5 MΩ / 1.0 MΩ + ADC input
// loading). 0.73242188 = 3000 mV / 4096 (12-bit ADC with the internal 3.0 V
// reference). 1.73 is the empirical divider compensation factor RAK ships in
// their own examples — measured against a known supply, this lands within
// ±20 mV across the 3.3–4.2 V LiPo range.
static constexpr float VBAT_MV_PER_LSB     = 0.73242188f;
static constexpr float VBAT_DIVIDER_COMP   = 1.73f;
static constexpr float REAL_MV_PER_LSB     = VBAT_MV_PER_LSB * VBAT_DIVIDER_COMP;

void power_begin() {
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    // Discard the first reading — the SAADC needs a sample to settle after
    // a reference change.
    (void)analogRead(WB_A0);
}

uint16_t read_battery_mv() {
    // Average a handful of reads to knock down noise from the LoRa rail.
    uint32_t acc = 0;
    constexpr uint8_t N = 8;
    for (uint8_t i = 0; i < N; ++i) acc += analogRead(WB_A0);
    float mv = (acc / (float)N) * REAL_MV_PER_LSB;
    if (mv < 0)      return 0;
    if (mv > 65535)  return 65535;
    return (uint16_t)mv;
}

void deep_sleep_ms(uint32_t ms) {
    delay(ms);
}
