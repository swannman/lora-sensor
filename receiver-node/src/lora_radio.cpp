#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

#include "lora_radio.h"
#include "config.h"

static SX1262 radio = new Module(HELTEC_LORA_NSS, HELTEC_LORA_DIO1,
                                 HELTEC_LORA_RESET, HELTEC_LORA_BUSY);

// Set by the DIO1 ISR; cleared in poll() after we read the packet.
static volatile bool s_rx_flag = false;
static void IRAM_ATTR on_dio1() { s_rx_flag = true; }

bool LoraRadio::begin() {
    SPI.begin(HELTEC_LORA_SCK, HELTEC_LORA_MISO, HELTEC_LORA_MOSI, HELTEC_LORA_NSS);

    int16_t state = radio.begin(
        (float)LORA_FREQ_HZ / 1.0e6f,
        (float)LORA_BW_KHZ,
        (uint8_t)LORA_SF,
        (uint8_t)LORA_CR,
        (uint8_t)LORA_SYNC_PRIVATE,
        14,                             // TX power — unused on RX-only path
        (uint16_t)LORA_PREAMBLE_SYMBOLS,
        1.8f,                           // TCXO voltage
        false);                         // DC-DC regulator
    if (state != RADIOLIB_ERR_NONE) return false;

    if (radio.setDio2AsRfSwitch(true) != RADIOLIB_ERR_NONE) return false;
    if (radio.setCRC(2)               != RADIOLIB_ERR_NONE) return false;

    radio.setPacketReceivedAction(on_dio1);
    if (radio.startReceive() != RADIOLIB_ERR_NONE) return false;
    return true;
}

bool LoraRadio::poll(LoraRxFrame* out) {
    if (!s_rx_flag) return false;
    s_rx_flag = false;

    size_t len = radio.getPacketLength();
    if (len == 0 || len > sizeof(out->bytes)) {
        radio.startReceive();   // drop and re-arm
        return false;
    }
    int16_t state = radio.readData(out->bytes, len);
    out->len      = (uint8_t)len;
    out->rssi_dbm = (int16_t)radio.getRSSI();
    out->snr_x10  = (int16_t)(radio.getSNR() * 10.0f);
    radio.startReceive();       // re-arm for next packet
    return state == RADIOLIB_ERR_NONE;
}
