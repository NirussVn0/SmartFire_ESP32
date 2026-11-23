#pragma once
#include <cstdint>

namespace smartfire {
// All intervals must be below 2^31 ms. Unsigned subtraction survives millis wrap.
inline uint32_t elapsed(uint32_t now, uint32_t start) { return now - start; }
class HeldFor {
 public:
  bool update(bool condition, uint32_t now, uint32_t duration) {
    if (!condition) { active_ = false; return false; }
    if (!active_) { active_ = true; since_ = now; }
    return elapsed(now, since_) >= duration;
  }
 private:
  bool active_ = false;
  uint32_t since_ = 0;
};
struct Policy {
  uint16_t gas_warn = 1800; // Level 1 Warning
  uint16_t gas_on = 2400;   // Level 2 Alarm (EXAMPLE raw ADC counts; NOT ppm or a validated threshold.)
  uint16_t gas_off = 1600;
  bool mq2_enabled = true;
  bool flame_enabled = true;
  bool temp_enabled = true;
  float temp_warn_c = 45.0f;
  float temp_alarm_c = 60.0f;
  uint32_t flame_warn_ms = 150;
  uint32_t flame_hold_ms = 500;
  uint32_t gas_hold_ms = 2000;
  uint32_t temp_hold_ms = 2000;
  uint32_t clear_hold_ms = 10000;
  uint32_t mute_ms = 30000;
  uint32_t test_ms = 1000;
};
struct Inputs {
  uint16_t gas_raw = 0;
  bool flame = false;
  bool gas_ready = false;
  bool gas_fault = false;
  float temp_c = 0.0f;
  bool temp_ready = false;
};
enum class Command { Ack, Silence, Test };
enum class Result { Applied, Rejected };
enum class State { Warming, Unconfigured, Normal, Suspected, Alarm, Fault };
inline const char* stateName(State s) {
  switch (s) {
    case State::Warming: return "WARMING";
    case State::Unconfigured: return "UNCONFIGURED";
    case State::Normal: return "NORMAL";
    case State::Suspected: return "SUSPECTED";
    case State::Alarm: return "ALARM";
    case State::Fault: return "FAULT";
  }
  return "FAULT";
}
class AlarmCore {
 public:
  explicit AlarmCore(Policy policy = {}) : policy_(policy) {}
  void setPolicy(const Policy& policy) {
    if (policy.gas_off < policy.gas_on && policy.gas_warn < policy.gas_on &&
        policy.gas_on < 4090 && policy.temp_warn_c < policy.temp_alarm_c &&
        policy.flame_warn_ms <= policy.flame_hold_ms) {
      policy_ = policy;
    }
  }
  const Policy& policy() const { return policy_; }
  void update(const Inputs& in, uint32_t now) {
    in_ = in;
    const bool effFlame = policy_.flame_enabled && in.flame;
    const bool effGasReady = policy_.mq2_enabled && in.gas_ready;
    const bool effGasFault = policy_.mq2_enabled && in.gas_fault;
    const bool effTempReady = policy_.temp_enabled && in.temp_ready;

    // Level 1 warning timer for flame
    flameWarnActive_ = flameWarnTimer_.update(effFlame, now, policy_.flame_warn_ms);

    // Level 2 alarm timers (independent per-sensor)
    const bool flame_confirmed = flameTimer_.update(effFlame, now, policy_.flame_hold_ms);
    // A high rail still counts as gas evidence, even when also marked faulty.
    const bool gas_confirmed = gasTimer_.update(effGasReady && in.gas_raw >= policy_.gas_on,
                                                now, policy_.gas_hold_ms);
    const bool temp_confirmed = tempTimer_.update(effTempReady && in.temp_c >= policy_.temp_alarm_c,
                                                  now, policy_.temp_hold_ms);
    if (flame_confirmed || gas_confirmed || temp_confirmed) {
      if (!alarm_) { alarm_ = true; acknowledged_ = false; muteUsed_ = false; }
    }
    // Raw flame immediately cancels mute; confirmation still governs alarm entry.
    if (effFlame || effGasFault) muted_ = false;
    if (muted_ && elapsed(now, mutedAt_) >= policy_.mute_ms) muted_ = false;
    if (testing_ && elapsed(now, testAt_) >= policy_.test_ms) testing_ = false;
    const bool clear = !effFlame && !effGasFault &&
                       (!effGasReady || in.gas_raw <= policy_.gas_off) &&
                       (!effTempReady || in.temp_c <= policy_.temp_warn_c);
    if (clearTimer_.update(alarm_ && clear, now, policy_.clear_hold_ms)) {
      alarm_ = false; muted_ = false; acknowledged_ = false; muteUsed_ = false;
    }
  }
  Result command(Command cmd, uint32_t now) {
    if (cmd == Command::Ack && alarm_) { acknowledged_ = true; return Result::Applied; }
    if (cmd == Command::Silence && alarm_ && !(policy_.flame_enabled && in_.flame) &&
        !(policy_.mq2_enabled && in_.gas_fault) && !muteUsed_) {
      muted_ = true; muteUsed_ = true; mutedAt_ = now; return Result::Applied;
    }
    if (cmd == Command::Test && !alarm_ && !testing_) {
      testing_ = true; testAt_ = now; return Result::Applied;
    }
    return Result::Rejected;
  }
  State state(bool warming, bool commissioned) const {
    if (alarm_) return State::Alarm;
    if (policy_.mq2_enabled && in_.gas_fault) return State::Fault;
    if (flameWarnActive_ ||
        (policy_.mq2_enabled && in_.gas_ready && in_.gas_raw >= policy_.gas_warn) ||
        (policy_.temp_enabled && in_.temp_ready && in_.temp_c >= policy_.temp_warn_c)) {
      return State::Suspected;
    }
    if (warming) return State::Warming;
    if (!commissioned) return State::Unconfigured;
    return State::Normal;
  }
  bool alarm() const { return alarm_; }
  bool hornRequested() const { return (alarm_ && !muted_) || testing_; }
  bool muted() const { return muted_; }
  bool acknowledged() const { return acknowledged_; }
  bool testing() const { return testing_; }
 private:
  Policy policy_;
  Inputs in_;
  HeldFor flameWarnTimer_, flameTimer_, gasTimer_, tempTimer_, clearTimer_;
  bool flameWarnActive_ = false;
  bool alarm_ = false, muted_ = false, muteUsed_ = false;
  bool acknowledged_ = false, testing_ = false;
  uint32_t mutedAt_ = 0, testAt_ = 0;
};
}  // namespace smartfire
