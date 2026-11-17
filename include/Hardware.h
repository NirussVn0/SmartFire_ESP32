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
  smartfire::Inputs sample(uint32_t now) {
    // 8 readings reduce ADC noise without seconds-long blocking or auto-learning hazards.
    uint32_t sum = 0;
    for (int i = 0; i < 8; ++i) sum += analogRead(config::kGasPin);
    const uint16_t raw = sum / 8;
    const bool railFault = railTimer_.update(raw <= 5 || raw >= 4090, now, 5000);
    return {raw, digitalRead(config::kFlamePin) == (config::kFlameActiveLow ? LOW : HIGH),
            !warming(now) && config::kGasCommissioned, railFault};
  }
  bool warming(uint32_t now) const { return smartfire::elapsed(now, bootAt_) < config::kWarmupMs; }
  void output(const smartfire::AlarmCore& core, smartfire::State state, uint32_t now) {
    const bool on = config::kHornEnabled && core.hornRequested();
    digitalWrite(config::kHornPin, on == config::kHornActiveHigh ? HIGH : LOW);
    const bool led = core.hornRequested() || (state == smartfire::State::Normal ?
                     (now % 2000 < 100) : (now % 500 < 250));
    digitalWrite(config::kLedPin, led ? HIGH : LOW);
  }
  // Debounced short release -> Silence. Hold >=2s -> a one-second output Test.
  bool button(uint32_t now, smartfire::Command& out) {
    const bool raw = digitalRead(config::kButtonPin) == LOW;
    if (raw != lastRaw_) { lastRaw_ = raw; changedAt_ = now; }
    if (smartfire::elapsed(now, changedAt_) >= 40 && stable_ != raw) {
      stable_ = raw;
      if (stable_) { pressedAt_ = now; longSent_ = false; }
      else if (!longSent_) { out = smartfire::Command::Silence; return true; }
    }
    if (stable_ && !longSent_ && smartfire::elapsed(now, pressedAt_) >= 2000) {
      longSent_ = true; out = smartfire::Command::Test; return true;
    }
    return false;
  }
 private:
  smartfire::HeldFor railTimer_;
  uint32_t bootAt_ = 0, changedAt_ = 0, pressedAt_ = 0;
  bool lastRaw_ = false, stable_ = false, longSent_ = false;
};
