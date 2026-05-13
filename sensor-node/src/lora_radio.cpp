#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

#include "lora_radio.h"
#include "config.h"

// RAK4631 wires the SX1262 to a fixed set of nRF52840 pins; the RAKwireless
// variant.h doesn't expose PIN_LORA_* macros, so we define them here. Values
// come from RAK's published schematic / forum reference.
constexpr uint8_t PIN_LORA_NSS     = 42;  // P1.10 — chip select
constexpr uint8_t PIN_LORA_SCK     = 43;  // P1.11
constexpr uint8_t PIN_LORA_MOSI    = 44;  // P1.12
constexpr uint8_t PIN_LORA_MISO    = 45;  // P1.13
constexpr uint8_t PIN_LORA_BUSY    = 46;  // P1.14
constexpr uint8_t PIN_LORA_DIO1    = 47;  // P1.15 — interrupt
constexpr uint8_t PIN_LORA_NRESET  = 38;  // P1.06
// Powers the external SP3T RF switch IC. Without this driven HIGH the TX
// signal never reaches the antenna — DIO2 only selects TX vs RX inside the
// switch, it doesn't power it.
constexpr uint8_t PIN_LORA_ANT_PWR = 37;  // P1.05

static SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_NRESET, PIN_LORA_BUSY);

bool LoraRadio::begin() {
    pinMode(PIN_LORA_ANT_PWR, OUTPUT);
    digitalWrite(PIN_LORA_ANT_PWR, HIGH);

    // Adafruit's default SPI is wired to the IO-SLOT SPI pins (P0.03/29/30).
    // The SX1262 sits on a separate set of pins (P1.10/11/12/13), so we have
    // to retarget the SPI bus before begin() — otherwise RadioLib clocks
    // commands out to the wrong pins and reports CHIP_NOT_FOUND.
    SPI.setPins(PIN_LORA_MISO, PIN_LORA_SCK, PIN_LORA_MOSI);
    SPI.begin();

    // The RAK4631 runs an external TCXO at 1.8 V controlled by the SX1262's
    // DIO3 pin, and the antenna RF switch is steered by DIO2 — both must be
    // configured before begin() so the chip applies them on the first cal.
    int16_t state = radio.begin(
        (float)LORA_FREQ_HZ / 1.0e6f,
        (float)LORA_BW_KHZ,
        (uint8_t)LORA_SF,
        (uint8_t)LORA_CR,
        (uint8_t)LORA_SYNC_PRIVATE,
        (int8_t)LORA_TX_DBM,
        (uint16_t)LORA_PREAMBLE_SYMBOLS,
        1.8f,    // TCXO voltage
        false);  // useRegulatorLDO=false → DC-DC (RAK4631 has the DCDC inductor)
    Serial.printf("[lora] begin state=%d\n", (int)state);
    if (state != RADIOLIB_ERR_NONE) return false;

    int16_t rfsw = radio.setDio2AsRfSwitch(true);
    Serial.printf("[lora] dio2_rfsw state=%d\n", (int)rfsw);
    if (rfsw != RADIOLIB_ERR_NONE) return false;

    int16_t crc = radio.setCRC(2);
    Serial.printf("[lora] crc state=%d\n", (int)crc);
    if (crc != RADIOLIB_ERR_NONE) return false;

    radio.sleep();
    return true;
}

bool LoraRadio::transmit(const uint8_t* data, uint8_t len) {
    int16_t state = radio.transmit(const_cast<uint8_t*>(data), len);
    radio.sleep();
    return state == RADIOLIB_ERR_NONE;
}

void LoraRadio::sleep() {
    radio.sleep();
}
