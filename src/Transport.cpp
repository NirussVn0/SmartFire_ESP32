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

  float tempC = 28.5f;
  #if defined(temperatureRead)
  tempC = temperatureRead();
  if (tempC < 10.0f || tempC > 95.0f) tempC = 28.5f;
  #endif
  out += ",\"temperature_celsius\":" + String(tempC, 1);

  return out + '}';
}
void networkTask(void* arg) {
  auto& bus = *static_cast<Bus*>(arg);
  // Initialize BLE Provisioning Server
  ble_prov::Provisioner bleProv;
  bleProv.begin(secrets::kDeviceId);

  WebServer server(80);
  const char* headers[] = {"Authorization"};
  server.collectHeaders(headers, 1);
  Published local;

  String activeSsid, activePass;
  ble_prov::loadCredentials(activeSsid, activePass);
  bool credentialsSet = activeSsid.length() > 0 && strlen(secrets::kApiToken) >= 24;

  uint32_t requestId = 0;
  auto authorized = [&]() {
    if (server.header("Authorization") == String("Bearer ") + secrets::kApiToken) return true;
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
  server.onNotFound([&]() { server.send(404, "application/json", "{\"error\":\"not_found\"}"); });

  uint32_t retryAt = millis() - 15000u, logAt = millis();
  bool serverStarted = false;
  bool wasConnected = false;

  if (credentialsSet) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    Serial.printf("[WIFI] Initializing STA for SSID: '%s'\n", activeSsid.c_str());
  } else {
    Serial.println("NETWORK_DISABLED: connect via BLE to configure Wi-Fi credentials");
  }

  for (;;) {
    const uint32_t now = millis();

    // Check if new Wi-Fi credentials arrived via BLE
    String newSsid, newPass;
    if (bleProv.hasPendingUpdate(newSsid, newPass)) {
      activeSsid = newSsid;
      activePass = newPass;
      credentialsSet = true;
      WiFi.disconnect(true);
      delay(100);
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      serverStarted = false;
      wasConnected = false;
      retryAt = now - 15000u; // Reconnect immediately
    }

    if (credentialsSet && WiFi.status() != WL_CONNECTED && smartfire::elapsed(now, retryAt) >= 15000) {
      retryAt = now;
      Serial.printf("[WIFI] Connecting to '%s'...\n", activeSsid.c_str());
      WiFi.begin(activeSsid.c_str(), activePass.c_str());
    }

    if (credentialsSet && WiFi.status() == WL_CONNECTED) {
      if (!serverStarted) {
        server.enableCORS(true);
        server.begin();
        serverStarted = true;
      }
      if (!wasConnected) {
        wasConnected = true;
        String ipStr = WiFi.localIP().toString();
        Serial.printf("[WIFI] Connected! IP: %s\n", ipStr.c_str());

        if (MDNS.begin(secrets::kDeviceId) || MDNS.begin("smartfire")) {
          MDNS.addService("http", "tcp", 80);
          Serial.printf("[MDNS] Started: http://%s.local\n", secrets::kDeviceId);
        }

        bleProv.notifyStatus(
            String("{\"status\":\"CONNECTED\",\"ip\":\"") + ipStr +
            "\",\"device_id\":\"" + secrets::kDeviceId +
            "\",\"room_id\":\"" + secrets::kRoomId +
            "\",\"token\":\"" + secrets::kApiToken + "\"}"
        );
      }
      server.handleClient(); // Any HTTP client stall remains outside the alarm task.
    } else {
      if (wasConnected) {
        wasConnected = false;
        bleProv.notifyStatus("{\"status\":\"DISCONNECTED\"}");
      }
    }

    if (smartfire::elapsed(now, logAt) >= 1000) {
      logAt = now;
      if (xSemaphoreTake(bus.mutex, 0) == pdTRUE) {
        local = bus.published; xSemaphoreGive(bus.mutex);
        Serial.printf("ip=%s boot=%s ", (credentialsSet && WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str() : "disabled", bus.bootId);
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
