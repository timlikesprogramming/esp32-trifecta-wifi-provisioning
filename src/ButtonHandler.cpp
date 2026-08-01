#include "ButtonHandler.h"

ButtonHandler::ButtonHandler(uint8_t pin) : pin(pin) {}

void ButtonHandler::begin() {
    pinMode(pin, INPUT_PULLUP);
}

void ButtonHandler::onShortPress(std::function<void()> callback) {
    shortPressCallback = callback;
}

void ButtonHandler::onLongPress(std::function<void()> callback, unsigned long holdTimeMs) {
    longPressCallback = callback;
    longPressTimeMs = holdTimeMs;
}

void ButtonHandler::loop() {
    if (digitalRead(pin) == LOW) {
        if (!isButtonPressed) {
            delay(50); // Debounce
            if (digitalRead(pin) == LOW) {
                isButtonPressed = true;
                buttonPressTime = millis();
            }
        } else {
            if (longPressCallback && (millis() - buttonPressTime > longPressTimeMs)) {
                longPressCallback();
                // Prevent multiple triggers for a single long hold
                isButtonPressed = false; 
                buttonPressTime = millis() + 999999; // effectively disable until re-pressed
            }
        }
    } else {
        if (isButtonPressed) {
            isButtonPressed = false;
            unsigned long holdDuration = millis() - buttonPressTime;
            
            // If it was held for less than the long press threshold and wasn't manually disabled
            if (holdDuration < longPressTimeMs && buttonPressTime < millis()) {
                if (shortPressCallback) {
                    shortPressCallback();
                }
            }
        }
    }
}
