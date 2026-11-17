#pragma once
#include <Arduino.h>
#include <AlarmCore.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

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
  bool alarm = false, muted = false, acknowledged = false, testing = false;
  bool hornRequested = false;
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
  char bootId[17] = {};
};
bool startTransport(Bus& bus);
