// Battery measurement and low-power sleep helpers for the RAK4631.
#pragma once

#include <stdint.h>

// Configures the SAADC for VBAT reads. Call once from setup().
void power_begin();

// Reads the on-board 1.5M/1.0M battery divider through WB_A0 and returns
// VBAT in millivolts. ~4200 mV = full LiPo, ~3300 mV = empty (cut off here
// to avoid over-discharge).
uint16_t read_battery_mv();

// Sleeps the MCU for the requested duration. Adafruit's nRF52 BSP runs
// FreeRTOS with tickless idle, so delay() drops the core into System-ON
// sleep (~3–6 µA) until the RTC wakes us — no SoftDevice juggling needed.
void deep_sleep_ms(uint32_t ms);
