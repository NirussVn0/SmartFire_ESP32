#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "Secrets.h"

namespace ble_prov {

// Service & Characteristic UUIDs for SmartFire BLE Provisioning
constexpr char kServiceUuid[]        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr char kCharConfigUuid[]     = "beb5483e-36e1-4688-b7f5-ea07361b26a8"; // Write (JSON: {"ssid":"...","password":"..."})
constexpr char kCharStatusUuid[]     = "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"; // Read / Notify

inline void loadCredentials(String& outSsid, String& outPass) {
  Preferences prefs;
  prefs.begin("smartfire", true); // Read-only
  outSsid = prefs.getString("ssid", "");
  outPass = prefs.getString("pass", "");
  prefs.end();

  if (outSsid.isEmpty()) {
    outSsid = secrets::kWifiSsid;
    outPass = secrets::kWifiPassword;
  }
}

inline void saveCredentials(const String& ssid, const String& pass) {
  Preferences prefs;
  prefs.begin("smartfire", false); // Read-write
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

class Provisioner;

class ConfigCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit ConfigCallbacks(Provisioner* parent) : parent_(parent) {}
  void onWrite(BLECharacteristic* pChar) override;
 private:
  Provisioner* parent_;
};

class ServerCallbacks : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(Provisioner* parent) : parent_(parent) {}
  void onConnect(BLEServer* pServer) override;
  void onDisconnect(BLEServer* pServer) override;
 private:
  Provisioner* parent_;
};

class Provisioner {
 public:
  void begin(const char* deviceName) {
    BLEDevice::init(deviceName);
    server_ = BLEDevice::createServer();
    server_->setCallbacks(new ServerCallbacks(this));

    BLEService* service = server_->createService(kServiceUuid);

    charConfig_ = service->createCharacteristic(
        kCharConfigUuid,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    charConfig_->setCallbacks(new ConfigCallbacks(this));

    charStatus_ = service->createCharacteristic(
        kCharStatusUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    charStatus_->addDescriptor(new BLE2902());
    charStatus_->setValue("{\"status\":\"INITIALIZING\"}");

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(kServiceUuid);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);

    BLEAdvertisementData advData;
    advData.setName(deviceName);
    advData.setCompleteServices(BLEUUID(kServiceUuid));
    advertising->setAdvertisementData(advData);

    BLEDevice::startAdvertising();

    Serial.printf("[BLE] Provisioning server started as '%s'\n", deviceName);
  }

  void notifyStatus(const String& jsonStatus) {
    if (charStatus_) {
      charStatus_->setValue(jsonStatus.c_str());
      if (deviceConnected_) {
        charStatus_->notify();
      }
    }
  }

  bool hasPendingUpdate(String& outSsid, String& outPass) {
    if (pendingUpdate_) {
      outSsid = pendingSsid_;
      outPass = pendingPass_;
      pendingUpdate_ = false;
      return true;
    }
    return false;
  }

  void onConfigReceived(const String& raw) {
    Serial.printf("[BLE] Received config payload: %s\n", raw.c_str());
    String ssid, pass;
    // Extract JSON or "SSID\nPASS"
    int ssidKey = raw.indexOf("\"ssid\"");
    if (ssidKey >= 0) {
      int colon = raw.indexOf(':', ssidKey);
      int q1 = raw.indexOf('"', colon + 1);
      int q2 = raw.indexOf('"', q1 + 1);
      if (q1 >= 0 && q2 > q1) ssid = raw.substring(q1 + 1, q2);

      int passKey = raw.indexOf("\"password\"");
      if (passKey < 0) passKey = raw.indexOf("\"pass\"");
      if (passKey >= 0) {
        int colonP = raw.indexOf(':', passKey);
        int p1 = raw.indexOf('"', colonP + 1);
        int p2 = raw.indexOf('"', p1 + 1);
        if (p1 >= 0 && p2 > p1) pass = raw.substring(p1 + 1, p2);
      }
    } else {
      // Fallback: simple text "SSID\nPASSWORD"
      int newline = raw.indexOf('\n');
      if (newline >= 0) {
        ssid = raw.substring(0, newline);
        pass = raw.substring(newline + 1);
      } else {
        ssid = raw;
      }
    }

    ssid.trim();
    pass.trim();

    if (!ssid.isEmpty()) {
      Serial.printf("[BLE] Saving new Wi-Fi credentials: SSID='%s'\n", ssid.c_str());
      saveCredentials(ssid, pass);
      pendingSsid_ = ssid;
      pendingPass_ = pass;
      pendingUpdate_ = true;
      notifyStatus("{\"status\":\"CONNECTING\",\"ssid\":\"" + ssid + "\"}");
    }
  }

  void setConnected(bool connected) {
    deviceConnected_ = connected;
    if (!connected) {
      BLEDevice::startAdvertising();
      Serial.println("[BLE] Client disconnected, restarted advertising");
    } else {
      Serial.println("[BLE] Client connected via Bluetooth");
    }
  }

 private:
  BLEServer* server_ = nullptr;
  BLECharacteristic* charConfig_ = nullptr;
  BLECharacteristic* charStatus_ = nullptr;
  bool deviceConnected_ = false;
  bool pendingUpdate_ = false;
  String pendingSsid_;
  String pendingPass_;
};

inline void ConfigCallbacks::onWrite(BLECharacteristic* pChar) {
  String val = pChar->getValue().c_str();
  if (parent_ && !val.isEmpty()) {
    parent_->onConfigReceived(val);
  }
}

inline void ServerCallbacks::onConnect(BLEServer* /*pServer*/) {
  if (parent_) parent_->setConnected(true);
}

inline void ServerCallbacks::onDisconnect(BLEServer* /*pServer*/) {
  if (parent_) parent_->setConnected(false);
}

} // namespace ble_prov
