#pragma once
#include <cstdint>
namespace config {
constexpr int kGasPin = 34;
constexpr int kFlamePin = 27;
constexpr int kHornPin = 26;
constexpr int kButtonPin = 18;
constexpr int kLedPin = 25;
constexpr bool kFlameActiveLow = true; // Confirm on YOUR module before commissioning.
constexpr bool kHornActiveHigh = true; // Confirm D4184 logic before connecting the horn.
// Bench interlock: first flash drives LED and telemetry, NOT the 12V horn.
// Set true only after testing polarity with horn disconnected and using a pull resistor.
constexpr bool kHornEnabled = false;
// Set true after sensor conditioning, measured divider voltage, and threshold experiments.
constexpr bool kGasCommissioned = false;
constexpr bool kMq2Enabled = true;
constexpr bool kFlameEnabled = true;
constexpr bool kTempEnabled = true;
constexpr uint16_t kGasOff = 1600;
constexpr uint16_t kGasWarn = 1800; // Level 1 Warning (Cảnh báo sớm MQ-2)
constexpr uint16_t kGasOn = 2400;   // Level 2 Alarm (Báo cháy MQ-2). Illustrative 12-bit ADC counts.
constexpr float kTempWarnC = 45.0f;  // Level 1 Warning (°C)
constexpr float kTempAlarmC = 60.0f; // Level 2 Alarm (°C)
constexpr uint32_t kFlameWarnMs = 150;  // Level 1 Flame confirm (ms)
constexpr uint32_t kFlameAlarmMs = 500; // Level 2 Flame confirm (ms)
constexpr uint32_t kWarmupMs = 180000; // Boot inhibit only; NOT initial MQ-2 conditioning.
constexpr uint32_t kSampleMs = 50;
constexpr uint32_t kPublishMs = 200;
constexpr char kFirmwareVersion[] = "2.2.0-2level-sensors";
constexpr int8_t kDefaultTxPower1m = -45;
constexpr float kDefaultPathLossN = 2.4f;
static_assert(kGasOff < kGasWarn && kGasWarn < kGasOn && kGasOn < 4090, "Invalid gas hysteresis thresholds");
static_assert(kTempWarnC < kTempAlarmC, "Invalid temperature thresholds");
static_assert(kFlameWarnMs <= kFlameAlarmMs, "Invalid flame confirm intervals");
static_assert(kWarmupMs < 0x80000000u, "Interval too large");
}
