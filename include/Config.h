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
constexpr uint16_t kGasOn = 2400;  // Illustrative 12-bit ADC counts. Do NOT assume calibrated.
constexpr uint16_t kGasOff = 1900;
constexpr uint32_t kWarmupMs = 180000; // Boot inhibit only; NOT initial MQ-2 conditioning.
constexpr uint32_t kSampleMs = 50;
constexpr uint32_t kPublishMs = 200;
static_assert(kGasOff < kGasOn && kGasOn < 4090, "Invalid gas hysteresis thresholds");
static_assert(kWarmupMs < 0x80000000u, "Interval too large");
}
