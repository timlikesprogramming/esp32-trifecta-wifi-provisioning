#include "TrifectaProvisioner.h"
#include "HtmlTemplates.h"

TrifectaProvisioner* TrifectaProvisioner::instance = nullptr;

TrifectaProvisioner::TrifectaProvisioner() 
    : currentState(ProvisioningState::BOOTING),
      webServer(80),
      apIP(192, 168, 4, 1)
{
    instance = this;
}

void TrifectaProvisioner::WiFiEventStatic(WiFiEvent_t event, arduino_event_info_t info) {
    if (instance) {
        instance->handleWiFiEvent(event, info);
    }
}

void TrifectaProvisioner::handleWiFiEvent(WiFiEvent_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("\n[WiFi] Success! Connected with IP: ");
            Serial.println(WiFi.localIP());
            currentState = ProvisioningState::CONNECTED;
            reconnectInterval = 5000;
            startConnectedDashboard();
            break;

        case ARDUINO_EVENT_WPS_ER_SUCCESS:
            Serial.println("\n[WPS] Router found and paired securely!");
            esp_wifi_wps_disable();
            delay(50);
            currentState = ProvisioningState::CONNECTING_NEW_CREDS;
            connectStartTime = millis();
            WiFi.begin();
            break;

        case ARDUINO_EVENT_WPS_ER_FAILED:
        case ARDUINO_EVENT_WPS_ER_TIMEOUT:
            Serial.println("\n[WPS] Failed or Timed out. Rebooting to fallback state...");
            esp_wifi_wps_disable();
            delay(1000);
            ESP.restart();
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (currentState == ProvisioningState::CONNECTED) {
                Serial.println("\n[WiFi] Connection lost. Entering backoff reconnect loop...");
                currentState = ProvisioningState::RECONNECTING;
                lastReconnectAttempt = millis();
            }
            break;

        default:
            break;
    }
}

void TrifectaProvisioner::setupWPS() {
    wps_config.wps_type = WPS_TYPE_PBC;
    strcpy(wps_config.factory_info.manufacturer, "Waveshare");
    strcpy(wps_config.factory_info.model_number, "ESP32-S3");
    strcpy(wps_config.factory_info.model_name, "TrifectaProvisioner");
    strcpy(wps_config.factory_info.device_name, "MyDevice");
}

void TrifectaProvisioner::startWPSOverride() {
    Serial.println("\n[WPS] Hardware button pressed. Entering WPS Override Mode...");
    currentState = ProvisioningState::WPS_SEARCH;

    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    WiFi.mode(WIFI_STA);
    setupWPS();
    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0);

    Serial.println("[WPS] Searching... Press the WPS button on your home router NOW.");
}

void TrifectaProvisioner::handlePortalRoot() {
    if (currentState == ProvisioningState::CONNECTED) {
        webServer.send_P(200, "text/html", dashboard_html);
        return;
    }

    Serial.println("\n[Captive Portal] Client requested portal. Scanning Wi-Fi networks...");
    int n = WiFi.scanNetworks(false, true);
    String formFields = "";

    if (n > 0) {
        std::vector<String> seenSSIDs;
        String options = "";
        for (int i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;

            bool duplicate = false;
            for (const auto &s : seenSSIDs) {
                if (s == ssid) { duplicate = true; break; }
            }

            if (!duplicate) {
                seenSSIDs.push_back(ssid);
                int32_t rssi = WiFi.RSSI(i);
                String signal = (rssi >= -60) ? "Strong" : (rssi >= -75) ? "Good" : "Weak";
                options += "<option value=\"" + ssid + "\">" + ssid + " (" + String(rssi) + " dBm - " + signal + ")</option>\n";
            }
        }

        if (seenSSIDs.size() > 0) {
            formFields += "<label for=\"ssidSelect\">Wi-Fi Network</label>\n";
            formFields += "<select id=\"ssidSelect\" name=\"ssid\">\n" + options + "</select>\n";
            formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" style=\"display:none;\" disabled required>\n";
            formFields += "<button type=\"button\" id=\"manualBtn\" class=\"manual-btn\" onclick=\"toggleManual()\">Enter hidden SSID manually</button>\n";
        } else {
            formFields += "<label for=\"ssidInput\">Wi-Fi Network</label>\n";
            formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" required>\n";
        }
    } else {
        formFields += "<label for=\"ssidInput\">Wi-Fi Network</label>\n";
        formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" required>\n";
    }

    WiFi.scanDelete();

    String fullHTML = String(portal_header_html) + formFields + String(portal_footer_html);
    webServer.send(200, "text/html", fullHTML);
}

void TrifectaProvisioner::handlePortalConnect() {
    String ssid = webServer.arg("ssid");
    String pass = webServer.arg("pass");

    webServer.send_P(200, "text/html", success_html);
    Serial.printf("\n[Captive Portal] Credentials received for SSID: %s\n", ssid.c_str());

    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    currentState = ProvisioningState::CONNECTING_NEW_CREDS;
    connectStartTime = millis();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void TrifectaProvisioner::startConnectedDashboard() {
    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    webServer.on("/", HTTP_ANY, [this]() {
        webServer.send_P(200, "text/html", dashboard_html);
    });

    webServer.onNotFound([this]() {
        String message = "Not found: " + webServer.uri() + "\nMethod: " + String(webServer.method());
        Serial.println("[WebServer] 404 - " + message);
        webServer.send(404, "text/plain", message);
    });

    webServer.on("/api/status", HTTP_GET, [this]() {
        unsigned long sec = millis() / 1000;
        unsigned int hrs = sec / 3600;
        unsigned int mins = (sec % 3600) / 60;
        sec = sec % 60;
        char uptimeStr[32];
        snprintf(uptimeStr, sizeof(uptimeStr), "%02u:%02u:%02u", hrs, mins, (unsigned int)sec);

        String json = "{";
        json += "\"ssid\":\"" + WiFi.SSID() + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"mac\":\"" + WiFi.macAddress() + "\",";
        json += "\"uptime\":\"" + String(uptimeStr) + "\",";
        json += "\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",";
        json += "\"cpu\":" + String(ESP.getCpuFreqMHz());
        json += "}";

        webServer.send(200, "application/json", json);
    });

    webServer.on("/reset", HTTP_POST, [this]() {
        webServer.send(200, "text/plain", "Resetting...");
        delay(500);
        WiFi.disconnect(false, true);
        ESP.restart();
    });

    webServer.begin();
    Serial.print("\n[Dashboard] Web Dashboard active at http://");
    Serial.println(WiFi.localIP());
}

void TrifectaProvisioner::startDualBroadcast() {
    Serial.println("\n[Setup] No connection established. Launching Dual Broadcast...");
    WiFi.disconnect(); 
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);

    dnsServer.start(DNS_PORT, "*", apIP);

    webServer.on("/", HTTP_GET, std::bind(&TrifectaProvisioner::handlePortalRoot, this));
    webServer.on("/connect", HTTP_POST, std::bind(&TrifectaProvisioner::handlePortalConnect, this));

    webServer.on("/hotspot-detect.html", std::bind(&TrifectaProvisioner::handlePortalRoot, this));
    webServer.on("/generate_204", std::bind(&TrifectaProvisioner::handlePortalRoot, this));
    webServer.on("/connecttest.txt", std::bind(&TrifectaProvisioner::handlePortalRoot, this));
    webServer.on("/ncsi.txt", std::bind(&TrifectaProvisioner::handlePortalRoot, this));

    webServer.onNotFound([this]() {
        webServer.sendHeader("Location", String("http://") + apIP.toString(), true);
        webServer.send(302, "text/plain", "");
    });
    webServer.begin();

    improvBLE.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "MyProduct", "1.0.0", "MyProduct Setup");

    currentState = ProvisioningState::DUAL_BROADCAST;
    Serial.println("[Setup] Broadcasting BLE (Improv) and SoftAP (MyDevice_Setup)");
}

void TrifectaProvisioner::begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    WiFi.onEvent(TrifectaProvisioner::WiFiEventStatic);

    Serial.println("\n--- ESP32-S3 Commercial Provisioning ---");
    Serial.println("[Boot] Checking for saved networks...");
    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(500);
        Serial.print(".");
        attempts++;
        if (digitalRead(BUTTON_PIN) == LOW) {
            break;
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        currentState = ProvisioningState::CONNECTED;
        startConnectedDashboard();
    } else {
        startDualBroadcast();
    }
}

void TrifectaProvisioner::loop() {
    if (currentState == ProvisioningState::DUAL_BROADCAST || currentState == ProvisioningState::CONNECTED) {
        if (currentState == ProvisioningState::DUAL_BROADCAST) {
            dnsServer.processNextRequest();
        }
        webServer.handleClient();
    }

    if (currentState == ProvisioningState::CONNECTING_NEW_CREDS) {
        if (millis() - connectStartTime > CONNECT_TIMEOUT) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("\n[Error] Invalid password or router unreachable.");
                Serial.println("[Recovery] Erasing bad credentials and restarting setup...");
                WiFi.disconnect(false, true);
                delay(1000);
                ESP.restart();
            }
        }
    }

    if (currentState == ProvisioningState::RECONNECTING) {
        if (millis() - lastReconnectAttempt > reconnectInterval) {
            Serial.printf("[WiFi] Attempting reconnect... (Next attempt in %lu ms if failed)\n", reconnectInterval * 2);
            WiFi.reconnect();
            lastReconnectAttempt = millis();
            if (reconnectInterval < 60000) {
                reconnectInterval *= 2;
            }
        }
    }

    // Button Logic
    if (digitalRead(BUTTON_PIN) == LOW) {
        if (!isButtonPressed) {
            delay(50);
            if (digitalRead(BUTTON_PIN) == LOW) {
                isButtonPressed = true;
                buttonPressTime = millis();
            }
        } else {
            if (millis() - buttonPressTime > FACTORY_RESET_HOLD_TIME) {
                Serial.println("\n[Factory Reset] 5-second hold detected!");
                Serial.println("[Factory Reset] Erasing Wi-Fi credentials from NVM...");
                WiFi.disconnect(false, true);
                delay(1000);
                Serial.println("[Factory Reset] Complete. Rebooting device...");
                ESP.restart();
            }
        }
    } else {
        if (isButtonPressed) {
            isButtonPressed = false;
            unsigned long holdDuration = millis() - buttonPressTime;
            if (holdDuration < FACTORY_RESET_HOLD_TIME && currentState == ProvisioningState::DUAL_BROADCAST) {
                startWPSOverride();
            }
        }
    }
}
