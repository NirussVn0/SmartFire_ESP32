#include "Transport.h"
#include "Config.h"
#include "BleProvisioning.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

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

String statusJson(const Snapshot& s) {
  String out; out.reserve(650);
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
  out += ",\"horn_enabled\":" + String(boolean(config::kHornEnabled));
  out += ",\"gas_commissioned\":" + String(boolean(config::kGasCommissioned));
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
  uint32_t requestId = 0;
  bool apActive = false;

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
    server.send(200, "application/json", envelope() + ",\"latest_seq\":" + String(local.latestSeq) +
      ",\"status\":" + statusJson(local.status) + '}');
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
        ",\"status\":" + statusJson(e.snapshot) + '}';
    }
    server.send(200, "application/json", out + "]}");
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
          WiFi.begin(activeSsid.c_str(), activePass.c_str());
          lastStaAttempt = now;
          Serial.printf("[WIFI] Connecting to newly provided SSID: '%s'...\n", activeSsid.c_str());
        }

        if (WiFi.status() == WL_CONNECTED) {
          String ipStr = WiFi.localIP().toString();
          Serial.printf("[WIFI] Connected successfully! IP: %s\n", ipStr.c_str());
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
          Serial.println("[WIFI] Lost connection to router. Entering recovery/reconnect...");
          bleProv.notifyStatus("{\"status\":\"DISCONNECTED\"}");
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

        // Periodic background retry to last known good SSID if available (every 30s)
        if (!activeSsid.isEmpty() && smartfire::elapsed(now, lastStaAttempt) >= 30000) {
          lastStaAttempt = now;
          Serial.printf("[WIFI] Background retry to '%s'...\n", activeSsid.c_str());
          WiFi.begin(activeSsid.c_str(), activePass.c_str());
          if (WiFi.status() == WL_CONNECTED) {
            transition(WifiState::StaConnected);
          }
        }
        break;
      }
    }

    server.handleClient();

    // Periodic telemetry log to serial (redacting sensitive data)
    if (smartfire::elapsed(now, logAt) >= 1000) {
      logAt = now;
      if (xSemaphoreTake(bus.mutex, 0) == pdTRUE) {
        local = bus.published;
        xSemaphoreGive(bus.mutex);
        Serial.printf("ip=%s state=%s boot=%s ",
                      (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str() : "disabled",
                      wifiStateName(state), bus.bootId);
        Serial.println(statusJson(local.status));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

} // namespace

bool startTransport(Bus& bus) {
  return xTaskCreatePinnedToCore(networkTask, "smartfire-network", 20480, &bus, 1, nullptr, 0) == pdPASS;
}
