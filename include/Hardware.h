#pragma once
#include <Arduino.h>
#include <AlarmCore.h>
#include "Config.h"

class Hardware {
 public:
  void begin() {
    // Software cannot guarantee the gate voltage during reset: hardware pull-down/up required.
    digitalWrite(config::kHornPin, config::kHornActiveHigh ? LOW : HIGH);
    pinMode(config::kHornPin, OUTPUT);
    pinMode(config::kFlamePin, INPUT_PULLUP);
    pinMode(config::kButtonPin, INPUT_PULLUP);
    pinMode(config::kLedPin, OUTPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(config::kGasPin, ADC_11db);
    bootAt_ = millis();
  }
  smartfire::Inputs sample(uint32_t now, bool gasCommissioned = config::kGasCommissioned) {
    // 8 readings reduce ADC noise without seconds-long blocking or auto-learning hazards.
    uint32_t sum = 0;
    for (int i = 0; i < 8; ++i) sum += analogRead(config::kGasPin);
    const uint16_t raw = sum / 8;
    const bool railFault = railTimer_.update(raw <= 5 || raw >= 4090, now, 5000);
    return {raw, digitalRead(config::kFlamePin) == (config::kFlameActiveLow ? LOW : HIGH),
            !warming(now) && gasCommissioned, railFault};
  }
  bool warming(uint32_t now) const { return smartfire::elapsed(now, bootAt_) < config::kWarmupMs; }
  void output(const smartfire::AlarmCore& core, smartfire::State state, uint32_t now,
              bool hornEnabled = config::kHornEnabled) {
    const bool on = (state != smartfire::State::Disarmed) && hornEnabled && core.hornRequested();
    digitalWrite(config::kHornPin, on == config::kHornActiveHigh ? HIGH : LOW);
    const bool led = (state == smartfire::State::Disarmed) ? (now % 4000 < 80) :
                     core.hornRequested() || (state == smartfire::State::Normal ?
                     (now % 2000 < 100) : (now % 500 < 250));
    digitalWrite(config::kLedPin, led ? HIGH : LOW);
  }
  // Short release (<2s) -> 30s Silence. Release 2..4.9s -> 1s Test. Hold >=5s -> Toggle Disarm/Arm.
  bool button(uint32_t now, smartfire::Command& out) {
    const bool raw = digitalRead(config::kButtonPin) == LOW;
    if (raw != lastRaw_) { lastRaw_ = raw; changedAt_ = now; }
    if (smartfire::elapsed(now, changedAt_) >= 40 && stable_ != raw) {
      stable_ = raw;
      if (stable_) {
        pressedAt_ = now;
        hold5sSent_ = false;
      } else if (!hold5sSent_) {
        const uint32_t heldMs = smartfire::elapsed(now, pressedAt_);
        out = (heldMs >= 2000) ? smartfire::Command::Test : smartfire::Command::Silence;
        return true;
      }
    }
    if (stable_ && !hold5sSent_ && smartfire::elapsed(now, pressedAt_) >= 5000) {
      hold5sSent_ = true;
      out = smartfire::Command::ToggleDisarm;
      return true;
    }
    return false;
  }
 private:
  smartfire::HeldFor railTimer_;
  uint32_t bootAt_ = 0, changedAt_ = 0, pressedAt_ = 0;
  bool lastRaw_ = false, stable_ = false, hold5sSent_ = false;
};
