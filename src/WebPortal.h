#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <functional>

class WebPortal {
public:
    WebPortal();
    
    // Pass callbacks so the portal can notify the coordinator
    void onCredentialsReceived(std::function<void(String ssid, String pass)> callback);
    void onResetRequested(std::function<void()> callback);

    void startCaptivePortal(const char* apSsid, IPAddress apIp, byte dnsPort = 53);
    void startDashboard();
    void stop();
    void loop();

private:
    WebServer webServer;
    DNSServer dnsServer;
    bool isCaptivePortal;
    
    std::function<void(String, String)> credsCallback;
    std::function<void()> resetCallback;
    
    void handlePortalRoot();
    void handlePortalConnect();
};
