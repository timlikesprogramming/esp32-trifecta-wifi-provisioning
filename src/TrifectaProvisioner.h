#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "ImprovWiFiBLE.h"

#include "ButtonHandler.h"
#include "WpsManager.h"
#include "WebPortal.h"

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
    
    // Handlers
    ButtonHandler buttonHandler;
    WpsManager wpsManager;
    WebPortal webPortal;
    ImprovWiFiBLE improvBLE;

    // State
    ProvisioningState currentState;
    
    // Config
    const char* AP_SSID = "MyDevice_Setup";
    const char* MDNS_NAME = "mydevice";
    const byte DNS_PORT = 53;
    IPAddress apIP;

    // Connection tracking
    const unsigned long CONNECT_TIMEOUT = 20000;
    unsigned long connectStartTime = 0;
    unsigned long lastReconnectAttempt = 0;
    unsigned long reconnectInterval = 5000;
};
