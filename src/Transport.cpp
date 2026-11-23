#include "Transport.h"
#include "Config.h"
#include "BleProvisioning.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <cmath>

#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#include "Secrets.example.h"
#endif

namespace {

enum class WifiState {
  Booting,
  LoadCredentials,
  ConnectingSta,
  StaConnected,
  ProvisioningMode
};

const char* wifiStateName(WifiState s) {
  switch (s) {
    case WifiState::Booting: return "BOOTING";
    case WifiState::LoadCredentials: return "LOAD_CREDENTIALS";
    case WifiState::ConnectingSta: return "CONNECTING_STA";
    case WifiState::StaConnected: return "STA_CONNECTED";
    case WifiState::ProvisioningMode: return "PROVISIONING_MODE";
  }
  return "UNKNOWN";
}

String quote(const char* text) {
  String out = "\"";
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p == '"' || *p == '\\') { out += '\\'; out += static_cast<char>(*p); }
    else if (*p < 32) { char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", *p); out += escaped; }
    else out += static_cast<char>(*p);
  }
  return out + '"';
}

const char* boolean(bool b) { return b ? "true" : "false"; }

RuntimeConfig loadRuntimeConfigFromNvs() {
  RuntimeConfig cfg;
  Preferences prefs;
  if (prefs.begin("sf_cfg", true)) {
    const uint16_t gasWarn = prefs.getUShort("gas_warn", config::kGasWarn);
    const uint16_t gasOn = prefs.getUShort("gas_on", config::kGasOn);
    const uint16_t gasOff = prefs.getUShort("gas_off", config::kGasOff);
    if (gasOff < gasOn && gasWarn < gasOn && gasOn < 4090 && gasOff >= 200 && gasWarn >= 500) {
      cfg.gasWarn = gasWarn;
      cfg.gasOn = gasOn;
      cfg.gasOff = gasOff;
    }
    cfg.mq2Enabled = prefs.getBool("mq2_en", config::kMq2Enabled);
    cfg.flameEnabled = prefs.getBool("flm_en", config::kFlameEnabled);
    cfg.tempEnabled = prefs.getBool("tmp_en", config::kTempEnabled);
    const float tw = prefs.getFloat("tmp_w", config::kTempWarnC);
    const float ta = prefs.getFloat("tmp_a", config::kTempAlarmC);
    if (std::isfinite(tw) && std::isfinite(ta) && tw >= 25.0f && ta <= 120.0f && tw < ta) {
      cfg.tempWarnC = tw;
      cfg.tempAlarmC = ta;
    }
    const uint32_t fw = prefs.getUInt("flm_wms", config::kFlameWarnMs);
    const uint32_t fa = prefs.getUInt("flm_ams", config::kFlameAlarmMs);
    if (fw <= 3000 && fa >= 100 && fa <= 5000 && fw <= fa) {
      cfg.flameWarnMs = fw;
      cfg.flameAlarmMs = fa;
    }
    cfg.hornEnabled = prefs.getBool("horn_en", config::kHornEnabled);
    cfg.gasCommissioned = prefs.getBool("gas_comm", config::kGasCommissioned);
    const int8_t txPwr = static_cast<int8_t>(prefs.getChar("tx_pwr", config::kDefaultTxPower1m));
    if (txPwr >= -75 && txPwr <= -20) cfg.txPower1m = txPwr;
    const float n = prefs.getFloat("pl_n", config::kDefaultPathLossN);
    if (std::isfinite(n) && n >= 1.5f && n <= 4.5f) cfg.pathLossN = n;
    prefs.end();
  }
  cfg.revision = 1;
  return cfg;
}

void saveRuntimeConfigToNvs(const RuntimeConfig& cfg) {
  Preferences prefs;
  if (prefs.begin("sf_cfg", false)) {
    prefs.putUShort("gas_warn", cfg.gasWarn);
    prefs.putUShort("gas_on", cfg.gasOn);
    prefs.putUShort("gas_off", cfg.gasOff);
    prefs.putBool("mq2_en", cfg.mq2Enabled);
    prefs.putBool("flm_en", cfg.flameEnabled);
    prefs.putBool("tmp_en", cfg.tempEnabled);
    prefs.putFloat("tmp_w", cfg.tempWarnC);
    prefs.putFloat("tmp_a", cfg.tempAlarmC);
    prefs.putUInt("flm_wms", cfg.flameWarnMs);
    prefs.putUInt("flm_ams", cfg.flameAlarmMs);
    prefs.putBool("horn_en", cfg.hornEnabled);
    prefs.putBool("gas_comm", cfg.gasCommissioned);
    prefs.putChar("tx_pwr", cfg.txPower1m);
    prefs.putFloat("pl_n", cfg.pathLossN);
    prefs.end();
  }
}

bool extractJsonNumber(const String& body, const char* key, double& outVal) {
  const String needle = String("\"") + key + "\"";
  int idx = body.indexOf(needle);
  if (idx < 0) return false;
  idx = body.indexOf(':', idx + needle.length());
  if (idx < 0) return false;
  ++idx;
  while (idx < static_cast<int>(body.length()) && (body[idx] == ' ' || body[idx] == '\t')) ++idx;
  if (idx >= static_cast<int>(body.length())) return false;
  char* endPtr = nullptr;
  const char* startPtr = body.c_str() + idx;
  const double parsed = strtod(startPtr, &endPtr);
  if (endPtr == startPtr || !std::isfinite(parsed)) return false;
  outVal = parsed;
  return true;
}

bool extractJsonBool(const String& body, const char* key, bool& outVal) {
  const String needle = String("\"") + key + "\"";
  int idx = body.indexOf(needle);
  if (idx < 0) return false;
  idx = body.indexOf(':', idx + needle.length());
  if (idx < 0) return false;
  ++idx;
  while (idx < static_cast<int>(body.length()) && (body[idx] == ' ' || body[idx] == '\t')) ++idx;
  if (body.startsWith("true", idx)) { outVal = true; return true; }
  if (body.startsWith("false", idx)) { outVal = false; return true; }
  return false;
}

String configJson(const RuntimeConfig& cfg) {
  String out;
  out.reserve(420);
  out = "{\"gas_warn\":" + String(cfg.gasWarn);
  out += ",\"gas_on\":" + String(cfg.gasOn);
  out += ",\"gas_off\":" + String(cfg.gasOff);
  out += ",\"mq2_enabled\":" + String(boolean(cfg.mq2Enabled));
  out += ",\"flame_enabled\":" + String(boolean(cfg.flameEnabled));
  out += ",\"temp_enabled\":" + String(boolean(cfg.tempEnabled));
  out += ",\"temp_warn_c\":" + String(cfg.tempWarnC, 1);
  out += ",\"temp_alarm_c\":" + String(cfg.tempAlarmC, 1);
  out += ",\"flame_warn_ms\":" + String(cfg.flameWarnMs);
  out += ",\"flame_alarm_ms\":" + String(cfg.flameAlarmMs);
  out += ",\"horn_enabled\":" + String(boolean(cfg.hornEnabled));
  out += ",\"gas_commissioned\":" + String(boolean(cfg.gasCommissioned));
  out += ",\"tx_power_1m\":" + String(static_cast<int>(cfg.txPower1m));
  out += ",\"path_loss_n\":" + String(cfg.pathLossN, 2);
  out += ",\"firmware_version\":" + quote(config::kFirmwareVersion);
  out += ",\"revision\":" + String(cfg.revision);
  return out + '}';
}

String statusJson(const Snapshot& s, int wifiRssiSmoothed, int wifiRssiRaw,
                  uint32_t wifiReconnects, const RuntimeConfig& cfg) {
  String out; out.reserve(1150);
  out = "{\"uptime_ms\":" + String(s.uptimeMs);
  out += ",\"state\":" + quote(smartfire::stateName(s.state));
  out += ",\"gas_raw\":" + String(s.gasRaw);
  out += ",\"flame\":" + String(boolean(s.flame));
  out += ",\"gas_ready\":" + String(boolean(s.gasReady));
  out += ",\"gas_fault\":" + String(boolean(s.gasFault));
  out += ",\"alarm\":" + String(boolean(s.alarm));
  out += ",\"muted\":" + String(boolean(s.muted));
  out += ",\"acknowledged\":" + String(boolean(s.acknowledged));
  out += ",\"testing\":" + String(boolean(s.testing));
  out += ",\"horn_requested\":" + String(boolean(s.hornRequested));
  out += ",\"horn_enabled\":" + String(boolean(s.hornEnabled));
  out += ",\"gas_commissioned\":" + String(boolean(s.gasCommissioned));
  out += ",\"wifi_rssi\":" + String(wifiRssiSmoothed);
  out += ",\"wifi_rssi_raw\":" + String(wifiRssiRaw);
  out += ",\"wifi_reconnects\":" + String(wifiReconnects);
  out += ",\"gas_warn\":" + String(cfg.gasWarn);
  out += ",\"gas_on\":" + String(cfg.gasOn);
  out += ",\"gas_off\":" + String(cfg.gasOff);
  out += ",\"mq2_enabled\":" + String(boolean(cfg.mq2Enabled));
  out += ",\"flame_enabled\":" + String(boolean(cfg.flameEnabled));
  out += ",\"temp_enabled\":" + String(boolean(cfg.tempEnabled));
  out += ",\"temp_warn_c\":" + String(cfg.tempWarnC, 1);
  out += ",\"temp_alarm_c\":" + String(cfg.tempAlarmC, 1);
  out += ",\"flame_warn_ms\":" + String(cfg.flameWarnMs);
  out += ",\"flame_alarm_ms\":" + String(cfg.flameAlarmMs);
  out += ",\"tx_power_1m\":" + String(static_cast<int>(cfg.txPower1m));
  out += ",\"path_loss_n\":" + String(cfg.pathLossN, 2);
  out += ",\"firmware_version\":" + quote(config::kFirmwareVersion);
  // No fake temperature: null since no dedicated ambient temperature sensor is calibrated
  out += ",\"temperature_celsius\":null";
  return out + '}';
}

const char kSetupHtml[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SmartFire Setup Portal</title>
  <style>
    body { font-family: -apple-system, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; }
    .card { max-width: 420px; margin: 30px auto; background: #1e293b; border-radius: 14px; padding: 24px; box-shadow: 0 4px 16px rgba(0,0,0,0.4); }
    h2 { margin-top: 0; color: #38bdf8; }
    label { font-size: 13px; color: #94a3b8; display: block; margin-top: 14px; margin-bottom: 6px; }
    input[type=text], input[type=password], select { width: 100%; box-sizing: border-box; padding: 10px 12px; background: #0f172a; border: 1px solid #334155; border-radius: 8px; color: #fff; font-size: 15px; }
    button { width: 100%; margin-top: 22px; padding: 12px; background: #0284c7; color: white; border: none; border-radius: 8px; font-size: 16px; font-weight: bold; cursor: pointer; }
    button:hover { background: #0369a1; }
    .status { margin-top: 16px; font-size: 13px; color: #cbd5e1; padding: 10px; background: #0f172a; border-radius: 6px; }
  </style>
</head>
<body>
  <div class="card">
    <h2>SmartFire Setup</h2>
    <p style="font-size: 13px; color: #94a3b8;">Cấu hình kết nối Wi-Fi cho thiết bị cảm biến SmartFire.</p>
    <form method="POST" action="/configure">
      <label>Tên Wi-Fi (SSID):</label>
      <input type="text" name="ssid" id="ssid" placeholder="Nhập tên mạng Wi-Fi" required>
      <label>Mật khẩu Wi-Fi:</label>
      <input type="password" name="password" id="password" placeholder="Nhập mật khẩu Wi-Fi">
      <button type="submit">Lưu & Kết Nối</button>
    </form>
    <div class="status" id="statusBox">Trạng thái: Sẵn sàng cấu hình</div>
  </div>
</body>
</html>
)rawhtml";

void networkTask(void* arg) {
  auto& bus = *static_cast<Bus*>(arg);

  // Load persisted runtime sensor/RSSI calibration config from NVS and publish to sensing core
  RuntimeConfig liveCfg = loadRuntimeConfigFromNvs();
  if (xSemaphoreTake(bus.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    bus.runtimeConfig = liveCfg;
    xSemaphoreGive(bus.mutex);
  }

  // Initialize WiFi stack & LwIP tcpip_mbox before any socket or netif operations
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // Disable Wi-Fi modem sleep to prevent 100-400ms latency spikes & HTTP polling drops
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Derive unique device suffix from MAC address for SoftAP & BLE
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apSsid[32];
  snprintf(apSsid, sizeof(apSsid), "SmartFire-Setup-%02X%02X", mac[4], mac[5]);

  // Initialize BLE Provisioning Server
  ble_prov::Provisioner bleProv;
  bleProv.begin(secrets::kDeviceId);

  WebServer server(80);
  const char* headers[] = {"Authorization", "Content-Type"};
  server.collectHeaders(headers, 2);
  Published local;

  String activeSsid, activePass;
  String pendingSsid, pendingPass;
  bool hasNewCredentials = false;

  WifiState state = WifiState::Booting;
  uint32_t stateEnteredAt = millis();
  uint32_t lastStaAttempt = 0;
  uint32_t logAt = millis();
  uint32_t rssiSampleAt = 0;
  uint32_t requestId = 0;
  uint32_t wifiReconnects = 0;
  int wifiRssiRaw = -60;
  float wifiRssiFiltered = -60.0f;
  bool hasRssiSample = false;
  bool apActive = false;
  bool otaAuthorized = false;
  bool otaStarted = false;
  bool otaFailed = false;
  size_t otaBytesWritten = 0;
  String otaError;
  uint32_t pendingRebootAt = 0;

  auto transition = [&](WifiState next) {
    state = next;
    stateEnteredAt = millis();
    Serial.printf("[WIFI-SM] State -> %s\n", wifiStateName(next));
  };

  auto startSoftAp = [&]() {
    if (!apActive) {
      WiFi.mode(WIFI_AP_STA);
      // SoftAP with open or simple password setup
      WiFi.softAP(apSsid);
      IPAddress apIp = WiFi.softAPIP();
      Serial.printf("[WIFI-AP] SoftAP Started: '%s', IP: %s\n", apSsid, apIp.toString().c_str());
      apActive = true;
    }
  };

  auto stopSoftAp = [&]() {
    if (apActive) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      Serial.println("[WIFI-AP] SoftAP Stopped");
      apActive = false;
    }
  };

  // REST API Handlers
  auto authorized = [&]() {
    if (strlen(secrets::kApiToken) >= 24 &&
        server.header("Authorization") == String("Bearer ") + secrets::kApiToken) {
      return true;
    }
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
  };

  auto snapshot = [&]() {
    if (xSemaphoreTake(bus.mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
      server.send(503, "application/json", "{\"error\":\"busy\"}"); return false;
    }
    local = bus.published;
    xSemaphoreGive(bus.mutex);
    return true;
  };

  auto envelope = [&]() {
    return String("{\"api_version\":1,\"device_id\":") + quote(secrets::kDeviceId) +
      ",\"room_id\":" + quote(secrets::kRoomId) + ",\"boot_id\":" + quote(bus.bootId);
  };

  // Normal Status & Events Endpoints
  server.on("/v1/status", HTTP_GET, [&]() {
    if (!authorized() || !snapshot()) return;
    const int smoothedRssi = static_cast<int>(lroundf(wifiRssiFiltered));
    server.send(200, "application/json", envelope() + ",\"latest_seq\":" + String(local.latestSeq) +
      ",\"status\":" + statusJson(local.status, smoothedRssi, wifiRssiRaw, wifiReconnects, liveCfg) + '}');
  });

  server.on("/v1/events", HTTP_GET, [&]() {
    if (!authorized() || !snapshot()) return;
    const String sinceText = server.arg("since");
    uint64_t parsed = 0;
    for (size_t i = 0; i < sinceText.length(); ++i) {
      if (sinceText[i] < '0' || sinceText[i] > '9' || (parsed = parsed * 10 + sinceText[i] - '0') > UINT32_MAX) {
        server.send(400, "application/json", "{\"error\":\"invalid_since\"}"); return;
      }
    }
    const uint32_t since = static_cast<uint32_t>(parsed);
    const int smoothedRssi = static_cast<int>(lroundf(wifiRssiFiltered));
    String out = envelope() + ",\"latest_seq\":" + String(local.latestSeq) + ",\"oldest_seq\":" +
      String(local.count ? local.events[0].seq : 0) + ",\"events\":[";
    bool comma = false;
    for (size_t i = 0; i < local.count; ++i) {
      const auto& e = local.events[i];
      if (e.seq <= since) continue;
      if (comma) out += ',';
      comma = true;
      out += "{\"seq\":" + String(e.seq) + ",\"kind\":" + quote(e.kind) +
        ",\"request_id\":" + String(e.requestId) + ",\"result\":" + quote(e.result) +
        ",\"status\":" + statusJson(e.snapshot, smoothedRssi, wifiRssiRaw, wifiReconnects, liveCfg) + '}';
    }
    server.send(200, "application/json", out + "]}");
  });

  // Remote Runtime Configuration Endpoints (Save + Push from Android App over Wi-Fi)
  server.on("/v1/config", HTTP_GET, [&]() {
    if (!authorized()) return;
    server.send(200, "application/json", configJson(liveCfg));
  });

  server.on("/v1/config", HTTP_POST, [&]() {
    if (!authorized()) return;
    const String body = server.arg("plain");
    RuntimeConfig next = liveCfg;
    double numVal = 0.0;
    bool boolVal = false;

    if (extractJsonNumber(body, "gas_warn", numVal)) {
      const int v = static_cast<int>(lround(numVal));
      if (v < 500 || v > 3800) {
        server.send(400, "application/json", "{\"error\":\"invalid_gas_warn\"}"); return;
      }
      next.gasWarn = static_cast<uint16_t>(v);
    }
    if (extractJsonNumber(body, "gas_on", numVal)) {
      const int v = static_cast<int>(lround(numVal));
      if (v < 600 || v > 4000) {
        server.send(400, "application/json", "{\"error\":\"invalid_gas_on\"}"); return;
      }
      next.gasOn = static_cast<uint16_t>(v);
    }
    if (extractJsonNumber(body, "gas_off", numVal)) {
      const int v = static_cast<int>(lround(numVal));
      if (v < 200 || v > 3900) {
        server.send(400, "application/json", "{\"error\":\"invalid_gas_off\"}"); return;
      }
      next.gasOff = static_cast<uint16_t>(v);
    }
    if (next.gasWarn >= next.gasOn) {
      next.gasWarn = (next.gasOn > 450) ? static_cast<uint16_t>(next.gasOn - 250) : static_cast<uint16_t>(next.gasOn / 2);
    }
    if (next.gasOff >= next.gasWarn) {
      next.gasOff = (next.gasWarn > 350) ? static_cast<uint16_t>(next.gasWarn - 200) : static_cast<uint16_t>(next.gasWarn / 2);
    }
    if (next.gasOff >= next.gasOn) {
      // Auto-maintain safe hysteresis gap if caller only updated gas_on
      next.gasOff = (next.gasOn > 400) ? static_cast<uint16_t>(next.gasOn - 300) : static_cast<uint16_t>(next.gasOn / 2);
    }
    if (extractJsonBool(body, "mq2_enabled", boolVal)) {
      next.mq2Enabled = boolVal;
    }
    if (extractJsonBool(body, "flame_enabled", boolVal)) {
      next.flameEnabled = boolVal;
    }
    if (extractJsonBool(body, "temp_enabled", boolVal)) {
      next.tempEnabled = boolVal;
    }
    if (extractJsonNumber(body, "temp_warn_c", numVal)) {
      if (numVal < 25.0 || numVal > 95.0) {
        server.send(400, "application/json", "{\"error\":\"invalid_temp_warn_c\"}"); return;
      }
      next.tempWarnC = static_cast<float>(numVal);
    }
    if (extractJsonNumber(body, "temp_alarm_c", numVal)) {
      if (numVal < 35.0 || numVal > 120.0) {
        server.send(400, "application/json", "{\"error\":\"invalid_temp_alarm_c\"}"); return;
      }
      next.tempAlarmC = static_cast<float>(numVal);
    }
    if (next.tempWarnC >= next.tempAlarmC) {
      next.tempWarnC = (next.tempAlarmC >= 30.0f) ? (next.tempAlarmC - 5.0f) : 25.0f;
      if (next.tempWarnC >= next.tempAlarmC) {
        server.send(400, "application/json", "{\"error\":\"temp_warn_must_be_below_alarm\"}"); return;
      }
    }
    if (extractJsonNumber(body, "flame_warn_ms", numVal)) {
      const long v = lround(numVal);
      if (v < 0 || v > 3000) {
        server.send(400, "application/json", "{\"error\":\"invalid_flame_warn_ms\"}"); return;
      }
      next.flameWarnMs = static_cast<uint32_t>(v);
    }
    if (extractJsonNumber(body, "flame_alarm_ms", numVal)) {
      const long v = lround(numVal);
      if (v < 100 || v > 5000) {
        server.send(400, "application/json", "{\"error\":\"invalid_flame_alarm_ms\"}"); return;
      }
      next.flameAlarmMs = static_cast<uint32_t>(v);
    }
    if (next.flameWarnMs > next.flameAlarmMs) {
      next.flameWarnMs = next.flameAlarmMs;
    }
    if (extractJsonBool(body, "horn_enabled", boolVal)) {
      next.hornEnabled = boolVal;
    }
    if (extractJsonBool(body, "gas_commissioned", boolVal)) {
      next.gasCommissioned = boolVal;
    }
    if (extractJsonNumber(body, "tx_power_1m", numVal)) {
      const int v = static_cast<int>(lround(numVal));
      if (v < -75 || v > -20) {
        server.send(400, "application/json", "{\"error\":\"invalid_tx_power_1m\"}"); return;
      }
      next.txPower1m = static_cast<int8_t>(v);
    }
    if (extractJsonNumber(body, "path_loss_n", numVal)) {
      if (numVal < 1.5 || numVal > 4.5) {
        server.send(400, "application/json", "{\"error\":\"invalid_path_loss_n\"}"); return;
      }
      next.pathLossN = static_cast<float>(numVal);
    }

    next.revision = liveCfg.revision + 1;
    saveRuntimeConfigToNvs(next);
    if (xSemaphoreTake(bus.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      bus.runtimeConfig = next;
      xSemaphoreGive(bus.mutex);
    }
    liveCfg = next;
    Serial.printf("[CONFIG] Updated over Wi-Fi: mq2=%d(%u/%u/%u) temp=%d(%.1f/%.1f) flame=%d(%lu/%lu) horn=%d comm=%d tx=%d n=%.2f\n",
                  liveCfg.mq2Enabled ? 1 : 0, liveCfg.gasOff, liveCfg.gasWarn, liveCfg.gasOn,
                  liveCfg.tempEnabled ? 1 : 0, liveCfg.tempWarnC, liveCfg.tempAlarmC,
                  liveCfg.flameEnabled ? 1 : 0, static_cast<unsigned long>(liveCfg.flameWarnMs), static_cast<unsigned long>(liveCfg.flameAlarmMs),
                  liveCfg.hornEnabled ? 1 : 0, liveCfg.gasCommissioned ? 1 : 0,
                  static_cast<int>(liveCfg.txPower1m), liveCfg.pathLossN);
    server.send(200, "application/json", String("{\"applied\":true,\"config\":") + configJson(liveCfg) + '}');
  });

  // Wi-Fi OTA Firmware Update Endpoint (Push .bin from Android App)
  server.on(
      "/v1/ota",
      HTTP_POST,
      [&]() {
        const bool headerAuth = (strlen(secrets::kApiToken) >= 24 &&
                                 server.header("Authorization") == String("Bearer ") + secrets::kApiToken);
        const bool isAuth = otaStarted ? otaAuthorized : headerAuth;
        const bool started = otaStarted;
        const bool failed = otaFailed;
        const size_t bytesWritten = otaBytesWritten;
        const String errCopy = otaError;

        // Always reset per-request OTA state so stale flags never leak into subsequent requests
        otaAuthorized = false;
        otaStarted = false;
        otaFailed = false;
        otaBytesWritten = 0;
        otaError = "";

        if (!isAuth) {
          server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
          return;
        }
        if (!started || bytesWritten == 0) {
          server.send(400, "application/json", "{\"ota\":\"failed\",\"error\":\"empty_firmware_upload\"}");
          return;
        }
        if (failed || Update.hasError()) {
          server.send(500, "application/json",
                      String("{\"ota\":\"failed\",\"error\":") + quote(errCopy.c_str()) + '}');
          return;
        }
        server.sendHeader("Connection", "close");
        server.send(200, "application/json",
                    String("{\"ota\":\"ok\",\"rebooting\":true,\"bytes\":") + String(static_cast<unsigned>(bytesWritten)) +
                        ",\"previous_version\":" + quote(config::kFirmwareVersion) + '}');
        pendingRebootAt = millis() + 800;
      },
      [&]() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          otaStarted = true;
          otaFailed = false;
          otaBytesWritten = 0;
          otaError = "";
          otaAuthorized = (strlen(secrets::kApiToken) >= 24 &&
                           server.header("Authorization") == String("Bearer ") + secrets::kApiToken);
          if (!otaAuthorized) {
            otaFailed = true;
            otaError = "unauthorized";
            return;
          }
          Serial.printf("[OTA] Start firmware update: %s\n", upload.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            otaFailed = true;
            otaError = Update.errorString();
            Serial.printf("[OTA] begin failed: %s\n", otaError.c_str());
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (otaAuthorized && !otaFailed) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              otaFailed = true;
              otaError = Update.errorString();
              Serial.printf("[OTA] write failed: %s\n", otaError.c_str());
            } else {
              otaBytesWritten += upload.currentSize;
            }
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (otaAuthorized && !otaFailed) {
            if (otaBytesWritten == 0) {
              Update.abort();
              otaFailed = true;
              otaError = "empty_firmware_upload";
            } else if (Update.end(true)) {
              Serial.printf("[OTA] Success: %u bytes flashed\n", static_cast<unsigned>(upload.totalSize));
            } else {
              otaFailed = true;
              otaError = Update.errorString();
              Serial.printf("[OTA] end failed: %s\n", otaError.c_str());
            }
          }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          Update.abort();
          otaFailed = true;
          otaError = "aborted";
        }
      });

  auto enqueue = [&](smartfire::Command cmd) {
    if (!authorized()) return;
    const CommandRequest req{cmd, ++requestId, millis()};
    if (xQueueSend(bus.commands, &req, 0) != pdTRUE) {
      server.send(503, "application/json", "{\"error\":\"queue_full\"}"); return;
    }
    server.send(202, "application/json", String("{\"queued\":true,\"boot_id\":") +
      quote(bus.bootId) + ",\"request_id\":" + String(req.requestId) + '}');
  };

  server.on("/v1/commands/ack", HTTP_POST, [&]() { enqueue(smartfire::Command::Ack); });
  server.on("/v1/commands/silence", HTTP_POST, [&]() { enqueue(smartfire::Command::Silence); });
  server.on("/v1/commands/test", HTTP_POST, [&]() { enqueue(smartfire::Command::Test); });

  // Web Portal Endpoints for Setup / Recovery
  server.on("/", HTTP_GET, [&]() {
    server.send_P(200, "text/html", kSetupHtml);
  });

  server.on("/scan", HTTP_GET, [&]() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":" + quote(WiFi.SSID(i).c_str()) + ",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/configure", HTTP_POST, [&]() {
    if (server.hasArg("ssid")) {
      pendingSsid = server.arg("ssid");
      pendingPass = server.arg("password");
      pendingSsid.trim();
      pendingPass.trim();
      hasNewCredentials = true;
      server.send(200, "text/html", "<html><body><h3>Cai dat da duoc gui! ESP32 dang ket noi...</h3><p><a href='/status'>Kiem tra trang thai</a></p></body></html>");
      Serial.printf("[PORTAL] New Wi-Fi configuration submitted for SSID: '%s'\n", pendingSsid.c_str());
    } else {
      server.send(400, "text/plain", "Missing ssid parameter");
    }
  });

  server.on("/status", HTTP_GET, [&]() {
    String out = "{\"wifi_state\":" + quote(wifiStateName(state)) +
                 ",\"connected\":" + String(boolean(WiFi.status() == WL_CONNECTED)) +
                 ",\"ip\":" + quote(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "") +
                 ",\"ap_ssid\":" + quote(apSsid) + "}";
    server.send(200, "application/json", out);
  });

  server.onNotFound([&]() {
    if (state == WifiState::ProvisioningMode) {
      // Captive portal redirect
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "application/json", "{\"error\":\"not_found\"}");
    }
  });

  server.enableCORS(true);
  server.begin();
  Serial.println("[HTTP] Web server initialized on port 80");

  transition(WifiState::LoadCredentials);

  for (;;) {
    const uint32_t now = millis();

    // Check if new Wi-Fi credentials arrived via BLE
    String bleSsid, blePass;
    if (bleProv.hasPendingUpdate(bleSsid, blePass)) {
      pendingSsid = bleSsid;
      pendingPass = blePass;
      hasNewCredentials = true;
      Serial.printf("[BLE] New credentials queued for SSID: '%s'\n", pendingSsid.c_str());
    }

    // State Machine transitions
    switch (state) {
      case WifiState::Booting:
        transition(WifiState::LoadCredentials);
        break;

      case WifiState::LoadCredentials: {
        ble_prov::loadCredentials(activeSsid, activePass);
        if (!activeSsid.isEmpty()) {
          Serial.printf("[WIFI] Found configured SSID: '%s'. Attempting connection...\n", activeSsid.c_str());
          WiFi.mode(WIFI_STA);
          WiFi.setSleep(false);
          WiFi.setTxPower(WIFI_POWER_19_5dBm);
          WiFi.setAutoReconnect(true);
          WiFi.begin(activeSsid.c_str(), activePass.c_str());
          lastStaAttempt = now;
          transition(WifiState::ConnectingSta);
        } else {
          Serial.println("[WIFI] No valid credentials found. Entering Provisioning/Recovery mode...");
          transition(WifiState::ProvisioningMode);
        }
        break;
      }

      case WifiState::ConnectingSta: {
        if (hasNewCredentials) {
          activeSsid = pendingSsid;
          activePass = pendingPass;
          hasNewCredentials = false;
          WiFi.disconnect(true);
          vTaskDelay(pdMS_TO_TICKS(100));
          WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
          WiFi.setSleep(false);
          WiFi.setTxPower(WIFI_POWER_19_5dBm);
          WiFi.begin(activeSsid.c_str(), activePass.c_str());
          lastStaAttempt = now;
          Serial.printf("[WIFI] Connecting to newly provided SSID: '%s'...\n", activeSsid.c_str());
        }

        if (WiFi.status() == WL_CONNECTED) {
          WiFi.setSleep(false);
          String ipStr = WiFi.localIP().toString();
          Serial.printf("[WIFI] Connected successfully! IP: %s RSSI: %d dBm\n", ipStr.c_str(), WiFi.RSSI());
          // Save verified working credentials to NVS
          ble_prov::saveCredentials(activeSsid, activePass);

          if (MDNS.begin(secrets::kDeviceId) || MDNS.begin("smartfire")) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("[MDNS] Started: http://%s.local\n", secrets::kDeviceId);
          }

          // Notify BLE status without leaking Bearer token
          bleProv.notifyStatus(
              String("{\"status\":\"CONNECTED\",\"ip\":\"") + ipStr +
              "\",\"device_id\":\"" + secrets::kDeviceId +
              "\",\"room_id\":\"" + secrets::kRoomId + "\"}"
          );

          stopSoftAp();
          transition(WifiState::StaConnected);
        } else if (smartfire::elapsed(now, stateEnteredAt) >= 25000) {
          // STA connection timeout -> Enter recovery mode
          Serial.println("[WIFI] Connection timed out (25s). Fallback to Recovery / Provisioning Mode...");
          bleProv.notifyStatus("{\"status\":\"DISCONNECTED\"}");
          transition(WifiState::ProvisioningMode);
        }
        break;
      }

      case WifiState::StaConnected: {
        if (WiFi.status() != WL_CONNECTED) {
          ++wifiReconnects;
          Serial.printf("[WIFI] Lost connection to router (#%lu). Reconnecting...\n",
                        static_cast<unsigned long>(wifiReconnects));
          bleProv.notifyStatus("{\"status\":\"DISCONNECTED\"}");
          WiFi.reconnect();
          lastStaAttempt = now;
          transition(WifiState::ConnectingSta);
          break;
        }

        if (hasNewCredentials) {
          transition(WifiState::ConnectingSta);
          break;
        }
        break;
      }

      case WifiState::ProvisioningMode: {
        startSoftAp();

        if (hasNewCredentials) {
          transition(WifiState::ConnectingSta);
          break;
        }

        // Continuously check if a prior background retry succeeded!
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("[WIFI] Background STA reconnect succeeded!");
          transition(WifiState::ConnectingSta);
          break;
        }

        // Periodic background retry to last known good SSID if available (every 20s)
        if (!activeSsid.isEmpty() && smartfire::elapsed(now, lastStaAttempt) >= 20000) {
          lastStaAttempt = now;
          Serial.printf("[WIFI] Background retry to '%s'...\n", activeSsid.c_str());
          WiFi.begin(activeSsid.c_str(), activePass.c_str());
        }
        break;
      }
    }

    // Smooth Wi-Fi RSSI every 250ms (EMA + spike clamp) for accurate indoor/outdoor distance estimation
    if (WiFi.status() == WL_CONNECTED && smartfire::elapsed(now, rssiSampleAt) >= 250) {
      rssiSampleAt = now;
      const int raw = WiFi.RSSI();
      if (raw < 0 && raw >= -100) {
        wifiRssiRaw = raw;
        if (!hasRssiSample) {
          wifiRssiFiltered = static_cast<float>(raw);
          hasRssiSample = true;
        } else {
          // Suppress sudden multipath spikes > 14 dBm in a single 250ms step
          const float delta = static_cast<float>(raw) - wifiRssiFiltered;
          const float clampedDelta = (delta > 14.0f) ? 14.0f : (delta < -14.0f ? -14.0f : delta);
          wifiRssiFiltered += 0.25f * clampedDelta;
        }
      }
    }

    server.handleClient();

    if (pendingRebootAt != 0 && static_cast<int32_t>(now - pendingRebootAt) >= 0) {
      Serial.println("[OTA] Rebooting into new firmware...");
      delay(50);
      ESP.restart();
    }

    // Periodic telemetry log to serial (redacting sensitive data)
    if (smartfire::elapsed(now, logAt) >= 1000) {
      logAt = now;
      if (xSemaphoreTake(bus.mutex, 0) == pdTRUE) {
        local = bus.published;
        xSemaphoreGive(bus.mutex);
        const int smoothedRssi = static_cast<int>(lroundf(wifiRssiFiltered));
        Serial.printf("ip=%s state=%s boot=%s ",
                      (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str() : "disabled",
                      wifiStateName(state), bus.bootId);
        Serial.println(statusJson(local.status, smoothedRssi, wifiRssiRaw, wifiReconnects, liveCfg));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

} // namespace

bool startTransport(Bus& bus) {
  return xTaskCreatePinnedToCore(networkTask, "smartfire-network", 20480, &bus, 1, nullptr, 0) == pdPASS;
}
