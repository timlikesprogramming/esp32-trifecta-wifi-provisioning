#include "WebPortal.h"
#include <WiFi.h>

void WebPortal::handleStatusApi() {
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
}
