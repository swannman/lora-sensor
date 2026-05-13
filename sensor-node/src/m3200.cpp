#include "m3200.h"
#include "config.h"

#include <nrf_gpio.h>

// --- Bit-bang I²C primitives (datasheet §1.8 forbids Sr) -------------------
void M3200::sda_in()  { pinMode(sda_pin_, INPUT_PULLUP); }
void M3200::sda_out() { pinMode(sda_pin_, OUTPUT); }

void M3200::iic_start() {
    sda_out();
    digitalWrite(sda_pin_, HIGH);
    digitalWrite(scl_pin_, HIGH);
    delayMicroseconds(4);
    digitalWrite(sda_pin_, LOW);    // SDA falling edge while SCL high = START
    delayMicroseconds(4);
    digitalWrite(scl_pin_, LOW);
}

void M3200::iic_stop() {
    sda_out();
    digitalWrite(scl_pin_, LOW);
    digitalWrite(sda_pin_, LOW);
    delayMicroseconds(4);
    digitalWrite(scl_pin_, HIGH);
    digitalWrite(sda_pin_, HIGH);   // SDA rising edge while SCL high = STOP
    delayMicroseconds(4);
}

bool M3200::iic_wait_ack() {
    uint16_t err = 0;
    sda_in();
    digitalWrite(sda_pin_, HIGH);
    delayMicroseconds(1);
    digitalWrite(scl_pin_, HIGH);
    delayMicroseconds(1);
    while (digitalRead(sda_pin_)) {
        if (++err > 250) {
            iic_stop();
            return false;
        }
    }
    digitalWrite(scl_pin_, LOW);
    return true;
}

void M3200::iic_ack() {
    digitalWrite(scl_pin_, LOW);
    sda_out();
    digitalWrite(sda_pin_, LOW);
    delayMicroseconds(2);
    digitalWrite(scl_pin_, HIGH);
    delayMicroseconds(2);
    digitalWrite(scl_pin_, LOW);
}

void M3200::iic_nack() {
    digitalWrite(scl_pin_, LOW);
    sda_out();
    digitalWrite(sda_pin_, HIGH);
    delayMicroseconds(2);
    digitalWrite(scl_pin_, HIGH);
    delayMicroseconds(2);
    digitalWrite(scl_pin_, LOW);
}

void M3200::iic_send_byte(uint8_t txd) {
    sda_out();
    digitalWrite(scl_pin_, LOW);
    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(sda_pin_, (txd & 0x80) ? HIGH : LOW);
        txd <<= 1;
        delayMicroseconds(2);
        digitalWrite(scl_pin_, HIGH);
        delayMicroseconds(2);
        digitalWrite(scl_pin_, LOW);
        delayMicroseconds(2);
    }
}

uint8_t M3200::iic_read_byte(bool ack) {
    uint8_t v = 0;
    sda_in();
    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(scl_pin_, LOW);
        delayMicroseconds(2);
        digitalWrite(scl_pin_, HIGH);
        v <<= 1;
        if (digitalRead(sda_pin_)) v++;
        delayMicroseconds(1);
    }
    if (ack) iic_ack();
    else     iic_nack();
    return v;
}

// --- Public API ------------------------------------------------------------
void M3200::calibrate(uint8_t n) {
    int16_t saved = offset_cpsi_;
    offset_cpsi_ = 0;                  // measure raw values during calibration
    int32_t sum = 0;
    uint8_t valid = 0;
    for (uint8_t i = 0; i < n; ++i) {
        M3200Reading r = sample();
        if (r.ok) { sum += r.pressure_cpsi; ++valid; }
        delay(80);
    }
    if (valid == 0) {
        offset_cpsi_ = saved;
        return;
    }
    int32_t avg = sum / valid;
    // Sanity: ±3 PSI. Outside that, the sensor is probably pressurized at
    // boot or returning garbage — keep offset at zero rather than baking in
    // a bad calibration.
    if (avg < -300 || avg > 300) offset_cpsi_ = 0;
    else                          offset_cpsi_ = (int16_t)avg;
}

void M3200::begin() {
    // Sensor V+ on power_pin_, configured for H0H1 high-drive (nRF52840
    // standard drive guarantees only 0.5 mA @ VOH=VDD-0.4V; we need 3.5 mA).
    nrf_gpio_cfg(
        g_ADigitalPinMap[power_pin_],
        NRF_GPIO_PIN_DIR_OUTPUT,
        NRF_GPIO_PIN_INPUT_DISCONNECT,
        NRF_GPIO_PIN_NOPULL,
        NRF_GPIO_PIN_H0H1,
        NRF_GPIO_PIN_NOSENSE);
    digitalWrite(power_pin_, HIGH);   // sensor stays powered; sleep current is ~5 µA

    // Idle bus state: both lines released high.
    pinMode(scl_pin_, OUTPUT); digitalWrite(scl_pin_, HIGH);
    pinMode(sda_pin_, OUTPUT); digitalWrite(sda_pin_, HIGH);

    delay(50);  // sensor warm-up before first MR

    // Hardcoded zero-offset measured at ambient on this specific unit.
    offset_cpsi_ = M3200_ZERO_OFFSET_CPSI;
}

M3200Reading M3200::sample() {
    M3200Reading r{};

    // Address byte for READ direction. Datasheet §1.6 specifies the MR
    // command must use the READ bit — Wire/TWIM can't send a true 0-byte
    // read with R bit set, which is why we bit-bang.
    const uint8_t addr_r = (uint8_t)((addr_ << 1) | 0x01);

    // Measurement Request: addr+R, no data, STOP. Triggers the sensor to
    // wake from sleep, take a measurement, and store the result.
    iic_start();
    iic_send_byte(addr_r);
    iic_wait_ack();
    iic_stop();

    delay(M3200_WAKEUP_MS);   // wait for measurement (datasheet: 8.4 ms)

    // Data Fetch DF4: read 4 bytes — pressure (14b) + temp (11b)
    iic_start();
    iic_send_byte(addr_r);
    if (!iic_wait_ack()) {
        r.status = M3200_FAULT;
        return r;
    }
    uint8_t buf[4];
    buf[0] = iic_read_byte(true);   // ACK
    buf[1] = iic_read_byte(true);   // ACK
    buf[2] = iic_read_byte(true);   // ACK
    buf[3] = iic_read_byte(false);  // NACK on last byte
    iic_stop();

    r.status       = (M3200Status)((buf[0] >> 6) & 0x03);
    r.raw_pressure = ((uint16_t)(buf[0] & 0x3F) << 8) | buf[1];
    r.raw_temp     = ((uint16_t)buf[2] << 3) | (buf[3] >> 5);

    // Datasheet text on p.6 (NOT the buggy formula in the sample code on
    // p.22 which omits the −P1 zero offset and produces a +7 PSI bias):
    //   counts = 14000/(Pmax-Pmin) × (Papplied-Pmin) + 1000
    //   ⇒ Papplied = (counts − 1000) × (Pmax-Pmin)/14000 + Pmin
    constexpr int32_t SPAN = M3200_PMAX_PSI - M3200_PMIN_PSI;
    int32_t cpsi = ((int32_t)r.raw_pressure - 1000) * 100L * SPAN / 14000L
                 + (int32_t)M3200_PMIN_PSI * 100L
                 - (int32_t)offset_cpsi_;       // zero-offset from calibrate()
    r.pressure_cpsi = (int16_t)constrain(cpsi, INT16_MIN, INT16_MAX);

    // Datasheet: T_C = counts × 200/2048 − 50 ⇒ cdegC = counts × 2000/2048 − 500
    int32_t cdegc = ((int32_t)r.raw_temp * 2000L / 2048L) - 500L;
    r.temp_cdegc = (int16_t)constrain(cdegc, INT16_MIN, INT16_MAX);

    r.ok = (r.status == M3200_OK || r.status == M3200_STALE);
    return r;
}
