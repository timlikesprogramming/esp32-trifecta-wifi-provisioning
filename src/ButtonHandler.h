#pragma once
#include <Arduino.h>
#include <functional>

class ButtonHandler {
public:
    ButtonHandler(uint8_t pin);
    
    void begin();
    void loop();
    
    void onShortPress(std::function<void()> callback);
    void onLongPress(std::function<void()> callback, unsigned long holdTimeMs = 5000);

private:
    uint8_t pin;
    unsigned long buttonPressTime = 0;
    bool isButtonPressed = false;
    unsigned long longPressTimeMs = 5000;
    
    std::function<void()> shortPressCallback;
    std::function<void()> longPressCallback;
};
