// Only compile BLE support if explicitly enabled
#ifdef IMPROV_WIFI_BLE_ENABLED

#include "ImprovWiFiBLE.h"

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#endif

// ==== ctor/dtor ====
ImprovWiFiBLE::~ImprovWiFiBLE() {
  // Minimal cleanup; NimBLEDevice::deinit() can be called by app if desired
}

// ==== public API (matching ImprovWiFi) ====

void ImprovWiFiBLE::setDeviceInfo(ImprovTypes::ChipFamily chipFamily,
                                  const char *firmwareName,
                                  const char *firmwareVersion,
                                  const char *deviceName,
                                  const char *deviceUrl) {
  chip_ = chipFamily;
  firmware_name_ = firmwareName ? firmwareName : "";
  firmware_version_ = firmwareVersion ? firmwareVersion : "";
  device_name_ = deviceName ? deviceName : "";
  device_friendly_name_ = deviceName ? deviceName : "";
  device_url_ = deviceUrl ? deviceUrl : "";

  // Initialize BLE and set up the Improv service
  NimBLEDevice::init(device_name_.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  // Set MTU to support larger payloads (like long SSIDs) without truncation
  NimBLEDevice::setMTU(256);

  // Prefer a robust address type on ESP32
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_RANDOM_DEFAULT);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(this);

  service_ = server_->createService(SVC_UUID);

  ch_state_ = service_->createCharacteristic(
      CHAR_STATE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  ch_error_ = service_->createCharacteristic(
      CHAR_ERROR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  ch_rpc_cmd_ = service_->createCharacteristic(
      CHAR_RPC_CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  ch_rpc_res_ = service_->createCharacteristic(
      CHAR_RPC_RES_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  ch_caps_ =
      service_->createCharacteristic(CHAR_CAPS_UUID, NIMBLE_PROPERTY::READ);

  ch_rpc_cmd_->setCallbacks(this);

  // Capabilities: bit 0 (0x01) Identify + bit 1 (0x02) Scan Supported = 0x03
  updateCaps(caps_);
  updateError(error_);
  updateState(state_);

  service_->start();

  adv_ = NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData advData = buildAdvData(state_, caps_);
  adv_->setAdvertisementData(advData);

  if (!device_name_.isEmpty()) {
    NimBLEAdvertisementData scan;
    scan.setName(device_name_.c_str());
    adv_->setScanResponseData(scan);
  }

  advertiseNow();
  adv_->start();
}

void ImprovWiFiBLE::setDeviceInfo(ImprovTypes::ChipFamily chipFamily,
                                  const char *firmwareName,
                                  const char *firmwareVersion,
                                  const char *deviceName) {
  setDeviceInfo(chipFamily, firmwareName, firmwareVersion, deviceName, nullptr);
}

void ImprovWiFiBLE::onImprovError(OnImprovError *errorCallback) {
  onImprovErrorCallback_ = errorCallback;
}

void ImprovWiFiBLE::onImprovConnected(OnImprovConnected *connectedCallback) {
  onImprovConnectedCallback_ = connectedCallback;
}

void ImprovWiFiBLE::setCustomConnectWiFi(
    CustomConnectWiFi *connectWiFiCallBack) {
  customConnectWiFiCallback_ = connectWiFiCallBack;
}

bool ImprovWiFiBLE::tryConnectToWifi(const char *ssid, const char *password) {
  return tryConnectToWifi(ssid, password, /*delayMs*/ 500, /*maxAttempts*/ 20);
}

bool ImprovWiFiBLE::tryConnectToWifi(const char *ssid, const char *password,
                                     uint32_t delayMs, uint8_t maxAttempts) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.begin(ssid, password);

  for (uint8_t i = 0; i < maxAttempts; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(delayMs);
  }
  return (WiFi.status() == WL_CONNECTED);
}

bool ImprovWiFiBLE::isConnected() { return WiFi.status() == WL_CONNECTED; }

// ==== NimBLE callbacks ====

void ImprovWiFiBLE::onDisconnect(NimBLEServer *server, NimBLEConnInfo& connInfo, int reason) {
  if (adv_) {
    advertiseNow();
  }
  // The safest way to restart advertising in NimBLE after a disconnect
  NimBLEDevice::startAdvertising();
}

void ImprovWiFiBLE::onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) {
  if (c != ch_rpc_cmd_)
    return;
  const std::string v = c->getValue();
  if (v.size() < 3) {
    updateError(ERR_BAD_PACKET);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    return;
  }
  handleRpc(reinterpret_cast<const uint8_t *>(v.data()), v.size());
}

// ==== internal helpers / RPCs ====

void ImprovWiFiBLE::handleRpc(const uint8_t *data, size_t len) {
  const uint8_t cmd = data[0];
  const uint8_t declared_len = data[1];

  if (declared_len + 3 != len) {
    Serial.printf("\n[Improv BLE Error] Bad packet length! Expected %d, got %d\n", declared_len + 3, (int)len);
    updateError(ERR_BAD_PACKET);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    return;
  }

  const uint8_t cs = data[len - 1];
  if (checksumLSB(data, len - 1) != cs) {
    Serial.printf("\n[Improv BLE Error] Checksum error! Expected 0x%02X, got 0x%02X\n", checksumLSB(data, len - 1), cs);
    updateError(ERR_BAD_PACKET);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    return;
  }

  switch (cmd) {
  case 0x01: // Send Wi-Fi
    Serial.println("\n[Improv BLE] Received Wi-Fi credentials command (0x01)");
    rpcSendWifi(&data[2], declared_len);
    break;
  case 0x02: // Identify
    Serial.println("\n[Improv BLE] Received Identify command (0x02)");
    rpcIdentify();
    break;
  case 0x03: // Scan Wi-Fi (client RPC variant)
  case 0x04: // Scan Wi-Fi (official Improv BLE COMMAND_SCAN)
    Serial.printf("\n[Improv BLE] Received Wi-Fi scan command (0x%02X)\n", cmd);
    rpcScanWifi();
    break;
  default:
    Serial.printf("\n[Improv BLE Error] Unknown command byte: 0x%02X\n", cmd);
    updateError(ERR_UNKNOWN_CMD);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    break;
  }
}

static bool is_scanning = false;

void ImprovWiFiBLE::rpcScanWifi() {
  Serial.println("[Improv BLE] Spawning background task for Wi-Fi scan...");
  xTaskCreate([](void *pvParameters) {
    ImprovWiFiBLE *self = (ImprovWiFiBLE *)pvParameters;
    self->performScan();
    vTaskDelete(NULL);
  }, "wifi_scan", 4096, this, 1, NULL);
}

void ImprovWiFiBLE::performScan() {
  if (is_scanning) {
    Serial.println("[Improv BLE] Scan already in progress, ignoring duplicate request.");
    return;
  }
  is_scanning = true;

  Serial.println("[Improv BLE] Initiating 2.4GHz Wi-Fi scan...");
  
  // Retry mechanism for -2 (WIFI_SCAN_FAILED), which happens if the radio is busy
  int n = -2;
  for (int attempts = 0; attempts < 3; attempts++) {
    n = WiFi.scanNetworks(false, true);
    if (n >= 0) break;
    Serial.printf("[Improv BLE] Scan failed with %d, retrying...\n", n);
    delay(500);
  }
  
  Serial.printf("[Improv BLE] Wi-Fi scan complete. Found %d networks.\n", n);

  if (n > 0) {
    std::vector<String> seenSSIDs;
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;

      bool duplicate = false;
      for (const auto &s : seenSSIDs) {
        if (s == ssid) {
          duplicate = true;
          break;
        }
      }

      if (!duplicate) {
        seenSSIDs.push_back(ssid);
        String rssiStr = String(WiFi.RSSI(i));
        String authStr = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "NO" : "YES";

        Serial.printf("[Improv BLE] Streaming network -> SSID: %s | RSSI: %s | Auth: %s\n", ssid.c_str(), rssiStr.c_str(), authStr.c_str());

        std::vector<uint8_t> buf;
        // payload: ssid_len (1) + ssid + rssi (1) + auth (1)
        uint8_t payload_len = 1 + ssid.length() + 1 + 1;
        buf.reserve(3 + payload_len);

        buf.push_back(0x04);
        buf.push_back(payload_len);

        buf.push_back((uint8_t)ssid.length());
        buf.insert(buf.end(), ssid.begin(), ssid.end());

        buf.push_back((uint8_t)(WiFi.RSSI(i)));
        buf.push_back((uint8_t)(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1));
        buf.push_back(checksumLSB(buf.data(), buf.size()));

        ch_rpc_res_->setValue((uint8_t *)buf.data(), buf.size());
        ch_rpc_res_->notify();
        delay(50); 
      }
    }
  }

  WiFi.scanDelete();

  Serial.println("[Improv BLE] Sending scan completion signal (0x04, 0x00)");
  std::vector<uint8_t> endBuf = {0x04, 0x00};
  endBuf.push_back(checksumLSB(endBuf.data(), endBuf.size()));
  ch_rpc_res_->setValue((uint8_t *)endBuf.data(), endBuf.size());
  ch_rpc_res_->notify();
  
  is_scanning = false;
}

void ImprovWiFiBLE::rpcSendWifi(const uint8_t *p, size_t n) {
  if (n < 2) {
    updateError(ERR_BAD_PACKET);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    return;
  }

  const uint8_t ssid_len = p[0];
  if (1 + ssid_len > n) {
    updateError(ERR_BAD_PACKET);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    return;
  }
  String ssid;
  ssid.reserve(ssid_len);
  for (uint8_t i = 0; i < ssid_len; i++)
    ssid += (char)p[1 + i];

  size_t pos = 1 + ssid_len;
  if (pos >= n) {
    updateError(ERR_BAD_PACKET);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    return;
  }
  const uint8_t pass_len = p[pos];
  if (pos + 1 + pass_len > n) {
    updateError(ERR_BAD_PACKET);
    if (onImprovErrorCallback_)
      onImprovErrorCallback_(ImprovTypes::Error::ERROR_INVALID_RPC);
    return;
  }
  String pass;
  pass.reserve(pass_len);
  for (uint8_t i = 0; i < pass_len; i++)
    pass += (char)p[pos + 1 + i];

  // Update state immediately so the client knows we're trying to connect
  updateState(STATE_PROVISIONING);
  advertiseNow();

  // Create arguments for the background task
  String *args = new String[2];
  args[0] = ssid;
  args[1] = pass;
  void **params = new void*[2];
  params[0] = this;
  params[1] = args;

  xTaskCreate([](void *pvParameters) {
    void **p = (void **)pvParameters;
    ImprovWiFiBLE *self = (ImprovWiFiBLE *)p[0];
    String *args = (String *)p[1];

    bool ok = false;
    if (self->customConnectWiFiCallback_) {
      ok = self->customConnectWiFiCallback_(args[0].c_str(), args[1].c_str());
    } else {
      ok = self->tryConnectToWifi(args[0].c_str(), args[1].c_str());
    }

    if (!ok) {
      self->updateError(ERR_CONNECT);
      if (self->onImprovErrorCallback_)
        self->onImprovErrorCallback_(ImprovTypes::Error::ERROR_UNABLE_TO_CONNECT);
      self->updateState(STATE_AUTHORIZED);
      self->advertiseNow();
    } else {
      if (self->onImprovConnectedCallback_) {
        self->onImprovConnectedCallback_(args[0].c_str(), args[1].c_str());
      }
      self->updateState(STATE_PROVISIONED);
      self->advertiseNow();
      self->sendDeviceUrl();
      delay(250);
    }

    delete[] args;
    delete[] p;
    vTaskDelete(NULL);
  }, "wifi_prov", 8192, params, 1, NULL);
}

void ImprovWiFiBLE::rpcIdentify() {
  Serial.println("\n[Improv BLE] Identify request received from web browser! Blinking LED...");
  // Blink status LED to visually identify the hardware unit
  pinMode(0, OUTPUT);
  for (int i = 0; i < 5; i++) {
    digitalWrite(0, LOW);
    delay(100);
    digitalWrite(0, HIGH);
    delay(100);
  }
}

void ImprovWiFiBLE::sendDeviceUrl() {
  std::string url = device_url_.c_str();
  if (url.empty() && WiFi.status() == WL_CONNECTED) {
    url = std::string("http://") + WiFi.localIP().toString().c_str();
  }

  std::vector<uint8_t> buf;
  buf.reserve(4 + url.length());
  buf.push_back(0x01);

  const uint8_t url_len = static_cast<uint8_t>(url.size());
  const uint8_t payload_len = 1 + url_len;
  buf.push_back(payload_len);
  buf.push_back(url_len);
  buf.insert(buf.end(), url.begin(), url.end());

  buf.push_back(checksumLSB(buf.data(), buf.size()));
  ch_rpc_res_->setValue((uint8_t *)buf.data(), buf.size());
  ch_rpc_res_->notify();
}

void ImprovWiFiBLE::updateState(uint8_t s) {
  state_ = s;
  if (ch_state_) {
    ch_state_->setValue(&state_, 1);
    ch_state_->notify();
  }
}

void ImprovWiFiBLE::updateError(uint8_t e) {
  error_ = e;
  if (ch_error_) {
    ch_error_->setValue(&error_, 1);
    ch_error_->notify();
  }
}

void ImprovWiFiBLE::updateCaps(uint8_t caps) {
  caps_ = caps;
  if (ch_caps_) {
    ch_caps_->setValue(&caps_, 1);
  }
}

NimBLEAdvertisementData ImprovWiFiBLE::buildAdvData(uint8_t state,
                                                    uint8_t caps) {
  NimBLEAdvertisementData ad;

  ad.setFlags(0x06);
  // Use 16-bit Improv Service UUID (0x4677) in primary ADV so payload is 19 bytes (well under 31B limit)
  ad.addServiceUUID(NimBLEUUID((uint16_t)SERVICE_DATA_UUID_16));

  uint8_t payload[8] = {0x77, 0x46, state, caps, 0x00, 0x00, 0x00, 0x00};
  ad.setServiceData(NimBLEUUID((uint16_t)SERVICE_DATA_UUID_16), payload,
                    sizeof(payload));

  return ad;
}

void ImprovWiFiBLE::advertiseNow() {
  if (!adv_)
    return;
  adv_->stop();

  NimBLEAdvertisementData advData = buildAdvData(state_, caps_);
  adv_->setAdvertisementData(advData);

  adv_->start();
}

uint8_t ImprovWiFiBLE::checksumLSB(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i < len; i++)
    sum += data[i];
  return static_cast<uint8_t>(sum & 0xFF);
}

#endif // IMPROV_WIFI_BLE_ENABLED
