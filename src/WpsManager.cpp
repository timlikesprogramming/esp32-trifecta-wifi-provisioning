#include "WpsManager.h"
#include <esp_wifi.h>

WpsManager::WpsManager() {
    wps_config.wps_type = WPS_TYPE_PBC;
    strcpy(wps_config.factory_info.manufacturer, "Waveshare");
    strcpy(wps_config.factory_info.model_number, "ESP32-S3");
    strcpy(wps_config.factory_info.model_name, "TrifectaProvisioner");
    strcpy(wps_config.factory_info.device_name, "MyDevice");
}

void WpsManager::start() {
    Serial.println("\n[WPS] Entering WPS Override Mode...");
    WiFi.mode(WIFI_STA);
    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0); // 120-second timeout
    Serial.println("[WPS] Searching... Press the WPS button on your home router NOW.");
}

void WpsManager::stop() {
    esp_wifi_wps_disable();
}
