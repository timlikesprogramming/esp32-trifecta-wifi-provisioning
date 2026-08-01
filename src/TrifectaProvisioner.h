#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_wps.h>
#include "ImprovWiFiBLE.h"

enum class ProvisioningState {
    BOOTING,
    DUAL_BROADCAST,
    CONNECTING_NEW_CREDS,
    WPS_SEARCH,
    CONNECTED,
    RECONNECTING
};

class TrifectaProvisioner {
public:
    TrifectaProvisioner();
    
    void begin();
    void loop();

private:
    static TrifectaProvisioner* instance;
    static void WiFiEventStatic(WiFiEvent_t event, arduino_event_info_t info);
    
    void handleWiFiEvent(WiFiEvent_t event, arduino_event_info_t info);
    
    // Setup Methods
    void setupWPS();
    void startWPSOverride();
    void startDualBroadcast();
    void startConnectedDashboard();

    // Web Handlers
    void handlePortalRoot();
    void handlePortalConnect();

    // State
    ProvisioningState currentState;
    
    // Servers
    WebServer webServer;
    DNSServer dnsServer;
    ImprovWiFiBLE improvBLE;
    
    // Config
    const char* AP_SSID = "MyDevice_Setup";
    const byte DNS_PORT = 53;
    IPAddress apIP;
    esp_wps_config_t wps_config;

    // Button & Timing logic
    const byte BUTTON_PIN = 0;
    unsigned long buttonPressTime = 0;
    bool isButtonPressed = false;
    const unsigned long FACTORY_RESET_HOLD_TIME = 5000;
    const unsigned long CONNECT_TIMEOUT = 20000;
    unsigned long connectStartTime = 0;
    unsigned long lastReconnectAttempt = 0;
    unsigned long reconnectInterval = 5000;
};
