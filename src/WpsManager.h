#pragma once
#include <Arduino.h>
#include <esp_wps.h>
#include <WiFi.h>

class WpsManager {
public:
    WpsManager();
    void start();
    void stop();

private:
    esp_wps_config_t wps_config;
};
