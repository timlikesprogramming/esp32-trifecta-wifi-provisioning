#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_wps.h>

#define IMPROV_WIFI_BLE_ENABLED
#include <ImprovWiFiBLE.h>

// --- HARDWARE CONFIGURATION ---
const byte BUTTON_PIN = 0; // BOOT button on Waveshare ESP32-S3

// --- TIMING & EDGE CASE CONFIGURATION ---
unsigned long buttonPressTime = 0;
bool isButtonPressed = false;
const unsigned long FACTORY_RESET_HOLD_TIME = 5000; // 5 seconds
const unsigned long CONNECT_TIMEOUT = 20000;        // 20 seconds for bad password

unsigned long connectStartTime = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectInterval = 5000; // Start reconnect backoff at 5s

// --- SOFTAP & PORTAL CONFIGURATION ---
const char *AP_SSID = "MyDevice_Setup";
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer webServer(80);

// --- IMPROV BLE CONFIGURATION ---
ImprovWiFiBLE improvBLE;

// --- STATE MACHINE ENUMS ---
enum State
{
    STATE_BOOTING,
    STATE_DUAL_BROADCAST,
    STATE_CONNECTING_NEW_CREDS,
    STATE_WPS_SEARCH,
    STATE_CONNECTED,
    STATE_RECONNECTING
};
State currentState = STATE_BOOTING;

// --- WPS CONFIGURATION ---
static esp_wps_config_t wps_config;

void setupWPS()
{
    wps_config.wps_type = WPS_TYPE_PBC;
    strcpy(wps_config.factory_info.manufacturer, "Waveshare");
    strcpy(wps_config.factory_info.model_number, "ESP32-S3");
    strcpy(wps_config.factory_info.model_name, "TrifectaProvisioner");
    strcpy(wps_config.factory_info.device_name, "MyDevice");
}

void startWPSOverride()
{
    Serial.println("\n[WPS] Hardware button pressed. Entering WPS Override Mode...");
    currentState = STATE_WPS_SEARCH;

    // Shut down concurrent servers to free memory and radio priority
    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    // Switch to strict Station mode for the blind search
    WiFi.mode(WIFI_STA);

    setupWPS();
    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0); // 120-second timeout

    Serial.println("[WPS] Searching... Press the WPS button on your home router NOW.");
}

// --- CAPTIVE PORTAL HTML (Stored in Flash Memory) ---
// EDGE CASE 1: Added explicit warning about 2.4GHz vs 5GHz networks
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: -apple-system, system-ui, sans-serif; background: #f4f4f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .card { background: white; padding: 2rem; border-radius: 12px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1); width: 90%; max-width: 400px; text-align: center; }
    input[type=text], input[type=password] { width: 100%; padding: 12px; margin: 8px 0; display: inline-block; border: 1px solid #ccc; border-radius: 6px; box-sizing: border-box; }
    input[type=submit] { width: 100%; background-color: #2563eb; color: white; padding: 14px 20px; margin: 16px 0; border: none; border-radius: 6px; cursor: pointer; font-size: 16px; font-weight: bold; }
    input[type=submit]:hover { background-color: #1d4ed8; }
    .warning { color: #d97706; font-size: 0.85rem; background: #fef3c7; padding: 10px; border-radius: 6px; margin-bottom: 15px;}
  </style>
</head>
<body>
  <div class="card">
    <h2>Connect Your Device</h2>
    <div class="warning"><strong>Note:</strong> Ensure you are connecting to a 2.4GHz network. 5GHz is not supported.</div>
    <form action="/connect" method="POST">
      <input type="text" name="ssid" placeholder="Wi-Fi Network Name" required>
      <input type="password" name="pass" placeholder="Password" required>
      <input type="submit" value="Connect Device">
    </form>
  </div>
</body>
</html>
)rawliteral";

const char success_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: -apple-system, system-ui, sans-serif; background: #f4f4f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .card { background: white; padding: 2rem; border-radius: 12px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1); text-align: center; }
  </style>
</head>
<body>
  <div class="card">
    <h2 style="color: #16a34a;">Credentials Received!</h2>
    <p>The device is now connecting to your network.</p>
    <p style="color: #666;">If the LED doesn't turn green in 30 seconds, hold the button for 5s to reset.</p>
  </div>
</body>
</html>
)rawliteral";

// --- WEB SERVER ROUTES ---
void handlePortalRoot()
{
    webServer.send_P(200, "text/html", index_html);
}

void handlePortalConnect()
{
    String ssid = webServer.arg("ssid");
    String pass = webServer.arg("pass");

    webServer.send_P(200, "text/html", success_html);
    Serial.printf("\n[Captive Portal] Credentials received for SSID: %s\n", ssid.c_str());

    // Shut down SoftAP state machine to focus resources on connecting
    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    // EDGE CASE 2: Set the state and timer to catch bad passwords
    currentState = STATE_CONNECTING_NEW_CREDS;
    connectStartTime = millis();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

// --- GLOBAL EVENT HANDLER ---
void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.print("\n[WiFi] Success! Connected with IP: ");
        Serial.println(WiFi.localIP());
        currentState = STATE_CONNECTED;
        reconnectInterval = 5000; // Reset exponential backoff on success
        break;

    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        Serial.println("\n[WPS] Router found and paired securely!");
        esp_wifi_wps_disable();
        delay(50);

        // EDGE CASE 2: Apply the timeout check to WPS too
        currentState = STATE_CONNECTING_NEW_CREDS;
        connectStartTime = millis();
        WiFi.begin();
        break;

    case ARDUINO_EVENT_WPS_ER_FAILED:
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        Serial.println("\n[WPS] Failed or Timed out. Rebooting to fallback state...");
        esp_wifi_wps_disable();
        delay(1000);
        ESP.restart();
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (currentState == STATE_CONNECTED)
        {
            // EDGE CASE 4: Trigger exponential backoff instead of immediate reconnect spam
            Serial.println("\n[WiFi] Connection lost. Entering backoff reconnect loop...");
            currentState = STATE_RECONNECTING;
            lastReconnectAttempt = millis();
        }
        break;

    default:
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    WiFi.onEvent(WiFiEvent);

    // STEP 1: Attempt to connect to previously saved Wi-Fi
    Serial.println("\n--- ESP32-S3 Commercial Provisioning ---");
    Serial.println("[Boot] Checking for saved networks...");
    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10)
    {
        delay(500);
        Serial.print(".");
        attempts++;
        if (digitalRead(BUTTON_PIN) == LOW)
        {
            break;
        }
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        currentState = STATE_CONNECTED;
        return;
    }

    // STEP 2: Launch Dual Broadcast Fallback
    Serial.println("\n[Setup] No connection established. Launching Dual Broadcast...");

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);

    dnsServer.start(DNS_PORT, "*", apIP);

    webServer.on("/", HTTP_GET, handlePortalRoot);
    webServer.on("/connect", HTTP_POST, handlePortalConnect);

    // EDGE CASE 3: Explicitly map OS background probes to keep captive portals alive
    webServer.on("/hotspot-detect.html", handlePortalRoot); // iOS
    webServer.on("/generate_204", handlePortalRoot);        // Android
    webServer.on("/connecttest.txt", handlePortalRoot);     // Windows / Microsoft
    webServer.on("/ncsi.txt", handlePortalRoot);            // Windows

    webServer.onNotFound([]()
                         {
        webServer.sendHeader("Location", String("http://") + apIP.toString(), true);
        webServer.send(302, "text/plain", ""); });
    webServer.begin();

    improvBLE.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "MyProduct", "1.0.0", "MyProduct Setup");

    currentState = STATE_DUAL_BROADCAST;
    Serial.println("[Setup] Broadcasting BLE (Improv) and SoftAP (MyDevice_Setup)");
}

void loop()
{
    // --- STATE MACHINE HANDLING ---
    if (currentState == STATE_DUAL_BROADCAST)
    {
        dnsServer.processNextRequest();
        webServer.handleClient();
    }

    // EDGE CASE 2: Bad Password / Connection Timeout Recovery
    if (currentState == STATE_CONNECTING_NEW_CREDS)
    {
        if (millis() - connectStartTime > CONNECT_TIMEOUT)
        {
            if (WiFi.status() != WL_CONNECTED)
            {
                Serial.println("\n[Error] Invalid password or router unreachable.");
                Serial.println("[Recovery] Erasing bad credentials and restarting setup...");
                WiFi.disconnect(false, true); // Erase bad NVM
                delay(1000);
                ESP.restart(); // Back to clean setup
            }
        }
    }

    // EDGE CASE 4: Exponential Backoff for Router Reboots
    if (currentState == STATE_RECONNECTING)
    {
        if (millis() - lastReconnectAttempt > reconnectInterval)
        {
            Serial.printf("[WiFi] Attempting reconnect... (Next attempt in %lu ms if failed)\n", reconnectInterval * 2);
            WiFi.reconnect();
            lastReconnectAttempt = millis();

            // Double the interval up to a maximum of 60 seconds
            if (reconnectInterval < 60000)
            {
                reconnectInterval *= 2;
            }
        }
    }

    // --- FACTORY RESET & WPS BUTTON LOGIC ---
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        if (!isButtonPressed)
        {
            delay(50); // Debounce
            if (digitalRead(BUTTON_PIN) == LOW)
            {
                isButtonPressed = true;
                buttonPressTime = millis();
            }
        }
        else
        {
            // Held for 5 seconds?
            if (millis() - buttonPressTime > FACTORY_RESET_HOLD_TIME)
            {
                Serial.println("\n[Factory Reset] 5-second hold detected!");
                Serial.println("[Factory Reset] Erasing Wi-Fi credentials from NVM...");
                WiFi.disconnect(false, true);
                delay(1000);
                Serial.println("[Factory Reset] Complete. Rebooting device...");
                ESP.restart();
            }
        }
    }
    else
    {
        if (isButtonPressed)
        {
            // Button released
            isButtonPressed = false;
            unsigned long holdDuration = millis() - buttonPressTime;

            // Short press during setup phase = Trigger WPS
            if (holdDuration < FACTORY_RESET_HOLD_TIME && currentState == STATE_DUAL_BROADCAST)
            {
                startWPSOverride();
            }
        }
    }
}