#include <Arduino.h>
#include <AlarmCore.h>
#include <esp_system.h>
#include "Config.h"
#include "Hardware.h"
#include "Transport.h"

namespace {
smartfire::Policy policy(const RuntimeConfig& cfg) {
  smartfire::Policy p;
  p.gas_warn = cfg.gasWarn;
  p.gas_on = cfg.gasOn;
  p.gas_off = cfg.gasOff;
  p.mq2_enabled = cfg.mq2Enabled;
  p.flame_enabled = cfg.flameEnabled;
  p.temp_enabled = cfg.tempEnabled;
  p.temp_warn_c = cfg.tempWarnC;
  p.temp_alarm_c = cfg.tempAlarmC;
  p.flame_warn_ms = cfg.flameWarnMs;
  p.flame_hold_ms = cfg.flameAlarmMs;
  return p;
}
RuntimeConfig activeCfg;
uint32_t appliedConfigRev = 0;
smartfire::AlarmCore core(policy(activeCfg));
Hardware hardware;
Bus bus;
Published pending;
Snapshot lastEvent;
bool haveEvent = false, transportReady = false;
uint32_t sampledAt = 0, publishedAt = 0;
Snapshot makeSnapshot(const smartfire::Inputs& in, uint32_t now) {
  return {now, in.gas_raw, core.state(hardware.warming(now), activeCfg.gasCommissioned),
          in.flame, in.gas_ready, in.gas_fault, core.alarm(), core.muted(),
          core.acknowledged(), core.testing(), core.hornRequested(),
          activeCfg.hornEnabled, activeCfg.gasCommissioned, activeCfg.gasOn, activeCfg.gasOff};
}
bool changed(const Snapshot& a, const Snapshot& b) {
  return a.state != b.state || a.alarm != b.alarm || a.muted != b.muted ||
         a.acknowledged != b.acknowledged || a.testing != b.testing ||
         a.gasFault != b.gasFault || a.gasReady != b.gasReady;
}
void record(const Snapshot& s, const char* kind, uint32_t id = 0, const char* result = "none") {
  if (pending.count == kEventCapacity) {
    for (size_t i = 1; i < pending.count; ++i) pending.events[i - 1] = pending.events[i];
    --pending.count;
  }
  pending.events[pending.count++] = {++pending.latestSeq, id, s, kind, result};
}
}
void setup() {
  hardware.begin(); // Set outputs before attempting any network operation.
  Serial.begin(115200);
  snprintf(bus.bootId, sizeof(bus.bootId), "%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  bus.mutex = xSemaphoreCreateMutex();
  bus.commands = xQueueCreate(8, sizeof(CommandRequest));
  transportReady = bus.mutex && bus.commands && startTransport(bus);
  if (!transportReady) Serial.println("NETWORK_TASK_FAILED: local sensing continues");
  sampledAt = millis() - config::kSampleMs;
}
void loop() {
  const uint32_t now = millis();
  if (smartfire::elapsed(now, sampledAt) < config::kSampleMs) { delay(1); return; }
  sampledAt = now;
  const auto in = hardware.sample(now, activeCfg.gasCommissioned);
  core.update(in, now);
  smartfire::Command buttonCommand;
  if (hardware.button(now, buttonCommand)) {
    const auto result = core.command(buttonCommand, now);
    record(makeSnapshot(in, now), "button", 0, result == smartfire::Result::Applied ? "applied" : "rejected");
  }
  // Bounded queue consumption: network cannot starve sensing.
  CommandRequest req;
  for (int i = 0; transportReady && i < 4 && xQueueReceive(bus.commands, &req, 0) == pdTRUE; ++i) {
    const uint32_t commandNow = millis();
    const bool stale = smartfire::elapsed(commandNow, req.enqueuedAt) > 2000;
    const auto result = stale ? smartfire::Result::Rejected : core.command(req.command, commandNow);
    const char* kind = req.command == smartfire::Command::Ack ? "ack" :
                       req.command == smartfire::Command::Silence ? "silence" : "test";
    record(makeSnapshot(in, now), kind, req.requestId,
           stale ? "expired" : result == smartfire::Result::Applied ? "applied" : "rejected");
  }
  const Snapshot current = makeSnapshot(in, now);
  hardware.output(core, current.state, now, activeCfg.hornEnabled);
  if (!haveEvent || changed(current, lastEvent)) {
    record(current, "state"); lastEvent = current; haveEvent = true;
  }
  pending.status = current;
  // Try-lock only: HTTP may delay telemetry but cannot hold up the local alarm loop.
  if (transportReady && smartfire::elapsed(now, publishedAt) >= config::kPublishMs &&
      xSemaphoreTake(bus.mutex, 0) == pdTRUE) {
    if (bus.runtimeConfig.revision != appliedConfigRev) {
      activeCfg = bus.runtimeConfig;
      appliedConfigRev = activeCfg.revision;
      core.setPolicy(policy(activeCfg));
    }
    bus.published = pending;
    xSemaphoreGive(bus.mutex);
    publishedAt = now;
  }
}
