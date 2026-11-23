#pragma once
#include <Arduino.h>
#include <AlarmCore.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "Config.h"

struct RuntimeConfig {
  uint16_t gasWarn = config::kGasWarn;
  uint16_t gasOn = config::kGasOn;
  uint16_t gasOff = config::kGasOff;
  bool mq2Enabled = config::kMq2Enabled;
  bool flameEnabled = config::kFlameEnabled;
  bool tempEnabled = config::kTempEnabled;
  float tempWarnC = config::kTempWarnC;
  float tempAlarmC = config::kTempAlarmC;
  uint32_t flameWarnMs = config::kFlameWarnMs;
  uint32_t flameAlarmMs = config::kFlameAlarmMs;
  bool hornEnabled = config::kHornEnabled;
  bool gasCommissioned = config::kGasCommissioned;
  int8_t txPower1m = config::kDefaultTxPower1m;
  float pathLossN = config::kDefaultPathLossN;
  uint32_t revision = 0;
};

struct CommandRequest {
  smartfire::Command command;
  uint32_t requestId;
  uint32_t enqueuedAt;
};
struct Snapshot {
  uint32_t uptimeMs = 0;
  uint16_t gasRaw = 0;
  smartfire::State state = smartfire::State::Warming;
  bool flame = false, gasReady = false, gasFault = false;
  bool alarm = false, muted = false, acknowledged = false, testing = false, disarmed = false;
  bool hornRequested = false;
  bool hornEnabled = config::kHornEnabled;
  bool gasCommissioned = config::kGasCommissioned;
  uint16_t gasOn = config::kGasOn;
  uint16_t gasOff = config::kGasOff;
};
struct Event {
  uint32_t seq = 0, requestId = 0;
  Snapshot snapshot;
  const char* kind = "state";
  const char* result = "none";
};
constexpr size_t kEventCapacity = 32;
struct Published {
  Snapshot status;
  Event events[kEventCapacity]; // Oldest first. RAM only, overwritten at capacity.
  size_t count = 0;
  uint32_t latestSeq = 0;
};
struct Bus {
  SemaphoreHandle_t mutex = nullptr;
  QueueHandle_t commands = nullptr;
  Published published;
  RuntimeConfig runtimeConfig;
  char bootId[17] = {};
};
bool startTransport(Bus& bus);
