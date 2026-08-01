#include <Arduino.h>
#include "TrifectaProvisioner.h"

TrifectaProvisioner provisioner;

void setup() {
    Serial.begin(115200);
    provisioner.begin();
}

void loop() {
    provisioner.loop();
}