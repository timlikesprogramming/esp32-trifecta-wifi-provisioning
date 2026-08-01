#include "TrifectaProvisioner.h"

TrifectaProvisioner* TrifectaProvisioner::instance = nullptr;

TrifectaProvisioner::TrifectaProvisioner() 
    : buttonHandler(0), // BOOT button on GPIO 0
      currentState(ProvisioningState::BOOTING),
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
            webPortal.startDashboard();
            break;

        case ARDUINO_EVENT_WPS_ER_SUCCESS:
            Serial.println("\n[WPS] Router found and paired securely!");
            wpsManager.stop();
            delay(50);
            currentState = ProvisioningState::CONNECTING_NEW_CREDS;
            connectStartTime = millis();
            WiFi.begin();
            break;

        case ARDUINO_EVENT_WPS_ER_FAILED:
        case ARDUINO_EVENT_WPS_ER_TIMEOUT:
            Serial.println("\n[WPS] Failed or Timed out. Rebooting to fallback state...");
            wpsManager.stop();
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

void TrifectaProvisioner::begin() {
    // Setup Button
    buttonHandler.begin();
    buttonHandler.onShortPress([this]() {
        if (currentState == ProvisioningState::DUAL_BROADCAST) {
            webPortal.stop();
            currentState = ProvisioningState::WPS_SEARCH;
            wpsManager.start();
        }
    });
    buttonHandler.onLongPress([]() {
        Serial.println("\n[Factory Reset] 5-second hold detected!");
        Serial.println("[Factory Reset] Erasing Wi-Fi credentials from NVM...");
        WiFi.disconnect(false, true);
        delay(1000);
        Serial.println("[Factory Reset] Complete. Rebooting device...");
        ESP.restart();
    }, 5000);

    // Setup WebPortal Callbacks
    webPortal.onCredentialsReceived([this](String ssid, String pass) {
        Serial.printf("\n[Captive Portal] Credentials received for SSID: %s\n", ssid.c_str());
        webPortal.stop();
        currentState = ProvisioningState::CONNECTING_NEW_CREDS;
        connectStartTime = millis();
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
    });
    
    webPortal.onResetRequested([]() {
        delay(500);
        WiFi.disconnect(false, true);
        ESP.restart();
    });

    // Setup WiFi
    WiFi.onEvent(TrifectaProvisioner::WiFiEventStatic);
    Serial.println("\n--- ESP32-S3 Commercial Provisioning ---");
    Serial.println("[Boot] Checking for saved networks...");
    WiFi.mode(WIFI_STA);
    WiFi.begin();

    // Wait for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(500);
        Serial.print(".");
        attempts++;
        buttonHandler.loop(); // Check button during boot
        if (currentState == ProvisioningState::WPS_SEARCH) {
            return; // Exit boot sequence early if button pressed
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        currentState = ProvisioningState::CONNECTED;
        webPortal.startDashboard();
    } else {
        Serial.println("\n[Setup] No connection established. Launching Dual Broadcast...");
        WiFi.disconnect(); 
        WiFi.mode(WIFI_AP_STA);
        
        webPortal.startCaptivePortal(AP_SSID, apIP, DNS_PORT);
        improvBLE.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "MyProduct", "1.0.0", "MyProduct Setup");
        
        currentState = ProvisioningState::DUAL_BROADCAST;
        Serial.println("[Setup] Broadcasting BLE (Improv) and SoftAP (MyDevice_Setup)");
    }
}

void TrifectaProvisioner::loop() {
    // Process Handlers
    buttonHandler.loop();

    if (currentState == ProvisioningState::DUAL_BROADCAST || currentState == ProvisioningState::CONNECTED) {
        webPortal.loop();
    }

    // Connection Timeout logic
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

    // Exponential Backoff Reconnect logic
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
}
