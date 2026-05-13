// Compile-time configuration. Defaults come from platformio.ini build_flags;
// override there or via PlatformIO env-specific sections.
#pragma once

#include <stdint.h>

// --- Identity --------------------------------------------------------------
#ifndef NODE_ID
#define NODE_ID 1
#endif

// --- Sample / TX cadence ---------------------------------------------------
// How often we wake to read the pressure sensor.
#ifndef SAMPLE_INTERVAL_S
#define SAMPLE_INTERVAL_S 30
#endif

// Force a TX at least this often even if pressure is unchanged (heartbeat).
#ifndef HEARTBEAT_INTERVAL_S
#define HEARTBEAT_INTERVAL_S 3600
#endif

// Smallest pressure delta (in centi-PSI = 0.01 PSI) that triggers a TX.
// 100 = 1.00 PSI. Lower = more responsive + more airtime.
#ifndef THRESHOLD_PSI_X100
#define THRESHOLD_PSI_X100 100
#endif

// Minimum seconds between transmissions, regardless of how fast pressure changes.
// Keeps a flapping sensor from saturating the channel and burning the battery.
#ifndef MIN_TX_COOLDOWN_S
#define MIN_TX_COOLDOWN_S 10
#endif

// --- Battery protection ----------------------------------------------------
// Below LOW_BATT_MV we stop event-driven TX and only emit the hourly
// heartbeat — keeps the LiPo out of the wear zone (cycling below ~3.0 V
// permanently reduces capacity).
#ifndef LOW_BATT_MV
#define LOW_BATT_MV 3300
#endif
// Below CRIT_BATT_MV we emit one last critical-batt packet and then
// long-sleep, only waking to re-check the battery. The cell's PCM will
// hard-cut at ~2.5 V if we somehow miss this; this firmware threshold is
// the soft cutoff that keeps the cell healthy.
#ifndef CRIT_BATT_MV
#define CRIT_BATT_MV 3000
#endif
// Sleep interval while in critical-battery state. Long enough that we don't
// chew through the last few mAh; short enough that recovery (e.g. fresh
// battery) is noticed within ~15 min.
#ifndef CRIT_SLEEP_S
#define CRIT_SLEEP_S 900
#endif
// Sample interval while in low-battery state. Drops the radio entirely; only
// wakes for a sensor read so we can still emit the hourly heartbeat.
#ifndef LOW_BATT_SAMPLE_S
#define LOW_BATT_SAMPLE_S 300
#endif

// --- Sensor ----------------------------------------------------------------
// I2C address of the M3200/M32JM family (TE/MEAS digital pressure modules).
#define M3200_I2C_ADDR 0x28

// Sensor full-scale pressure range in PSI (matches the -100PG suffix).
#define M3200_PMIN_PSI 0
#define M3200_PMAX_PSI 100

// Time from sensor power-on to first valid sample (datasheet: 8.4 ms in sleep mode).
#define M3200_WAKEUP_MS 12

// Zero-offset, measured at ambient on this specific sensor and hardcoded.
// Sensor's "zero pressure" output is spec'd 750-1250 counts (typ 1000); our
// unit reads ~975 counts at atmosphere, giving -25 cpsi (-0.25 PSI) before
// offset. Subtracting -25 cpsi (= adding 25) zeroes ambient. Re-run the
// 50-sample capture if you ever swap the sensor.
#define M3200_ZERO_OFFSET_CPSI (-25)

// --- LoRa ------------------------------------------------------------------
#ifndef LORA_FREQ_HZ
#define LORA_FREQ_HZ 915000000UL
#endif
#ifndef LORA_BW_KHZ
#define LORA_BW_KHZ 125
#endif
#ifndef LORA_SF
#define LORA_SF 9
#endif
#ifndef LORA_CR
#define LORA_CR 5    // 4/5
#endif
#ifndef LORA_TX_DBM
#define LORA_TX_DBM 14
#endif
#ifndef LORA_SYNC_PRIVATE
#define LORA_SYNC_PRIVATE 0x12  // private LoRa network; LoRaWAN public is 0x34
#endif
#ifndef LORA_PREAMBLE
#define LORA_PREAMBLE 12        // 12 symbols — extra preamble length for SF11 RX sync
#endif
#define LORA_PREAMBLE_SYMBOLS LORA_PREAMBLE
