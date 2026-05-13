// TE Connectivity M3200 / M32JM digital pressure transducer (I²C variant).
// Datasheet: ENG_DS_M3200_A20.pdf — 14-bit pressure, 11-bit temperature.
//
// Uses bit-banged I²C, NOT the Arduino Wire library. Per datasheet §1.8,
// the M3200's I²C interface forbids repeated-START (Sr) — and Adafruit's
// nRF52 Wire (built on the NRF_TWIM hardware peripheral) issues Sr in
// subtle ways that lock the sensor up. Bit-banging gives us full control
// over every SCL/SDA edge so we never produce an Sr.
#pragma once

#include <Arduino.h>

enum M3200Status : uint8_t {
    M3200_OK    = 0b00,  // normal operation, fresh data
    M3200_CMD   = 0b01,  // device in command mode
    M3200_STALE = 0b10,  // data already returned since last conversion
    M3200_FAULT = 0b11,  // diagnostic / fault
};

struct M3200Reading {
    bool        ok;
    M3200Status status;
    int16_t     pressure_cpsi;   // 0.01 PSI
    int16_t     temp_cdegc;      // 0.1 °C
    uint16_t    raw_pressure;    // 14-bit raw counts (1000..15000 spec range)
    uint16_t    raw_temp;        // 11-bit raw counts (0..2047)
};

class M3200 {
public:
    // sda/scl are Arduino pin numbers (not nRF GPIO numbers).
    // power_pin gates the sensor's V+ — kept HIGH continuously; the sensor
    // sits in 5 µA sleep mode between samples, costing ~0.12 mAh/day.
    M3200(uint8_t sda_pin, uint8_t scl_pin, uint8_t power_pin, uint8_t i2c_addr = 0x28)
        : sda_pin_(sda_pin), scl_pin_(scl_pin), power_pin_(power_pin), addr_(i2c_addr) {}

    void begin();

    // Take `n` samples at the current pressure and store their average as
    // the zero-offset. Sensor must be at ambient pressure when this runs.
    // If the average is outside ±300 cpsi (±3 PSI), the offset is left at
    // zero — likely the sensor is pressurized at boot or broken.
    void calibrate(uint8_t n = 16);
    int16_t offset_cpsi() const { return offset_cpsi_; }

    M3200Reading sample();

private:
    uint8_t sda_pin_;
    uint8_t scl_pin_;
    uint8_t power_pin_;
    uint8_t addr_;
    int16_t offset_cpsi_ = 0;

    void    sda_in();
    void    sda_out();
    void    iic_start();
    void    iic_stop();
    bool    iic_wait_ack();
    void    iic_ack();
    void    iic_nack();
    void    iic_send_byte(uint8_t txd);
    uint8_t iic_read_byte(bool ack);
};
