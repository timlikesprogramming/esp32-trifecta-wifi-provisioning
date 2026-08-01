#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_wps.h>

#ifndef IMPROV_WIFI_BLE_ENABLED
#define IMPROV_WIFI_BLE_ENABLED
#endif
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

// --- CAPTIVE PORTAL HTML TEMPLATES & ROUTING ---
const char portal_header_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MyDevice Setup</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #f4f4f5; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; padding: 1rem; box-sizing: border-box; }
    .card { background: white; padding: 2rem 1.5rem; border-radius: 16px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); width: 100%; max-width: 380px; text-align: center; }
    h2 { margin-top: 0; color: #18181b; font-size: 1.35rem; }
    .warning { color: #b45309; font-size: 0.825rem; background: #fef3c7; padding: 10px; border-radius: 8px; margin-bottom: 15px; text-align: left; line-height: 1.4; }
    label { display: block; text-align: left; font-size: 0.85rem; color: #52525b; font-weight: 600; margin-top: 10px; margin-bottom: 4px; }
    select, input[type=text], input[type=password] { width: 100%; padding: 12px; border: 1px solid #d4d4d8; border-radius: 8px; box-sizing: border-box; font-size: 0.95rem; background: #fafafa; }
    select:focus, input:focus { outline: none; border-color: #2563eb; background: #fff; }
    input[type=submit] { width: 100%; background-color: #2563eb; color: white; padding: 14px 20px; margin-top: 16px; border: none; border-radius: 8px; cursor: pointer; font-size: 1rem; font-weight: bold; transition: background 0.2s; }
    input[type=submit]:hover { background-color: #1d4ed8; }
    .manual-btn { margin-top: 10px; background: none; border: none; color: #2563eb; font-size: 0.825rem; cursor: pointer; text-decoration: underline; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Connect Your Device</h2>
    <div class="warning"><strong>Note:</strong> Select your 2.4GHz Wi-Fi network. 5GHz is not supported.</div>
    <form action="/connect" method="POST">
)rawliteral";

const char portal_footer_html[] PROGMEM = R"rawliteral(
      <label for="pass">Password</label>
      <input type="password" id="pass" name="pass" placeholder="Enter Wi-Fi Password" required>
      <input type="submit" value="Connect Device">
    </form>
  </div>
  <script>
    function toggleManual() {
      var sel = document.getElementById('ssidSelect');
      var inp = document.getElementById('ssidInput');
      var btn = document.getElementById('manualBtn');
      if (inp.style.display === 'none') {
        inp.style.display = 'block';
        inp.disabled = false;
        sel.style.display = 'none';
        sel.disabled = true;
        btn.innerText = 'Select from scanned networks';
      } else {
        inp.style.display = 'none';
        inp.disabled = true;
        sel.style.display = 'block';
        sel.disabled = false;
        btn.innerText = 'Enter hidden SSID manually';
      }
    }
  </script>
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
extern const char dashboard_html[];

void handlePortalRoot()
{
    if (currentState == STATE_CONNECTED) {
        webServer.send_P(200, "text/html", dashboard_html);
        return;
    }

    Serial.println("\n[Captive Portal] Client requested portal. Scanning Wi-Fi networks...");
    int n = WiFi.scanNetworks(false, true);

    String formFields = "";

    if (n > 0)
    {
        std::vector<String> seenSSIDs;
        String options = "";

        for (int i = 0; i < n; ++i)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;

            bool duplicate = false;
            for (const auto &s : seenSSIDs)
            {
                if (s == ssid)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
            {
                seenSSIDs.push_back(ssid);
                int32_t rssi = WiFi.RSSI(i);
                String signal = (rssi >= -60) ? "Strong" : (rssi >= -75) ? "Good" : "Weak";
                options += "<option value=\"" + ssid + "\">" + ssid + " (" + String(rssi) + " dBm - " + signal + ")</option>\n";
            }
        }

        if (seenSSIDs.size() > 0)
        {
            formFields += "<label for=\"ssidSelect\">Wi-Fi Network</label>\n";
            formFields += "<select id=\"ssidSelect\" name=\"ssid\">\n" + options + "</select>\n";
            formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" style=\"display:none;\" disabled required>\n";
            formFields += "<button type=\"button\" id=\"manualBtn\" class=\"manual-btn\" onclick=\"toggleManual()\">Enter hidden SSID manually</button>\n";
        }
        else
        {
            formFields += "<label for=\"ssidInput\">Wi-Fi Network</label>\n";
            formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" required>\n";
        }
    }
    else
    {
        formFields += "<label for=\"ssidInput\">Wi-Fi Network</label>\n";
        formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" required>\n";
    }

    WiFi.scanDelete();

    String fullHTML = String(portal_header_html) + formFields + String(portal_footer_html);
    webServer.send(200, "text/html", fullHTML);
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

// --- CONNECTED DASHBOARD HTML ---
const char dashboard_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 Dashboard</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 1.5rem; display: flex; justify-content: center; min-height: 100vh; }
    .dashboard { width: 100%; max-width: 580px; }
    .header { display: flex; justify-content: space-between; align-items: center; background: #1e293b; padding: 1.25rem 1.5rem; border-radius: 16px; margin-bottom: 1.25rem; border: 1px solid #334155; }
    .header h1 { margin: 0; font-size: 1.25rem; font-weight: 700; color: #fff; }
    .badge { background: rgba(34, 197, 94, 0.15); color: #4ade80; padding: 0.35rem 0.75rem; border-radius: 20px; font-size: 0.8rem; font-weight: 600; display: flex; align-items: center; gap: 6px; }
    .dot { width: 8px; height: 8px; background: #22c55e; border-radius: 50%; display: inline-block; animation: pulse 2s infinite; }
    @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.4; } 100% { opacity: 1; } }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 1rem; margin-bottom: 1.25rem; }
    @media (max-width: 480px) { .grid { grid-template-columns: 1fr; } }
    .card { background: #1e293b; border: 1px solid #334155; padding: 1.25rem; border-radius: 16px; }
    .card-title { font-size: 0.75rem; color: #94a3b8; text-transform: uppercase; font-weight: 700; letter-spacing: 0.05em; margin-bottom: 0.875rem; }
    .metric { display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.65rem; font-size: 0.875rem; }
    .metric:last-child { margin-bottom: 0; }
    .metric-label { color: #94a3b8; }
    .metric-value { font-weight: 600; color: #f8fafc; font-family: monospace; }
    .controls { background: #1e293b; border: 1px solid #334155; padding: 1.25rem; border-radius: 16px; text-align: center; }
    .btn { background: #2563eb; color: white; border: none; padding: 0.75rem 1.25rem; font-size: 0.9rem; font-weight: 600; border-radius: 10px; cursor: pointer; transition: all 0.2s; width: 100%; margin-bottom: 0.75rem; }
    .btn:hover { background: #1d4ed8; }
    .btn-danger { background: #dc2626; margin-bottom: 0; }
    .btn-danger:hover { background: #b91c1c; }
  </style>
</head>
<body>
  <div class="dashboard">
    <div class="header">
      <div>
        <h1>ESP32-S3 Dashboard</h1>
        <div style="font-size:0.8rem; color:#94a3b8; margin-top:2px;">Trifecta Provisioner</div>
      </div>
      <div class="badge"><span class="dot"></span> ONLINE</div>
    </div>

    <div class="grid">
      <div class="card">
        <div class="card-title">Network Status</div>
        <div class="metric"><span class="metric-label">SSID</span><span class="metric-value" id="ssid">--</span></div>
        <div class="metric"><span class="metric-label">IP Address</span><span class="metric-value" id="ip">--</span></div>
        <div class="metric"><span class="metric-label">Signal</span><span class="metric-value" id="rssi">--</span></div>
        <div class="metric"><span class="metric-label">MAC</span><span class="metric-value" id="mac">--</span></div>
      </div>
      <div class="card">
        <div class="card-title">System Metrics</div>
        <div class="metric"><span class="metric-label">Uptime</span><span class="metric-value" id="uptime">--</span></div>
        <div class="metric"><span class="metric-label">Free RAM</span><span class="metric-value" id="heap">--</span></div>
        <div class="metric"><span class="metric-label">CPU Speed</span><span class="metric-value" id="cpu">--</span></div>
        <div class="metric"><span class="metric-label">Status</span><span class="metric-value" style="color:#4ade80;">Active</span></div>
      </div>
    </div>

    <div class="controls">
      <button class="btn" onclick="triggerPing()">Ping Device</button>
      <button class="btn btn-danger" onclick="resetWiFi()">Reset Wi-Fi Credentials</button>
    </div>
  </div>

  <script>
    async function updateStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        document.getElementById('ssid').innerText = data.ssid;
        document.getElementById('ip').innerText = data.ip;
        document.getElementById('rssi').innerText = data.rssi + ' dBm';
        document.getElementById('mac').innerText = data.mac;
        document.getElementById('uptime').innerText = data.uptime;
        document.getElementById('heap').innerText = data.heap + ' KB';
        document.getElementById('cpu').innerText = data.cpu + ' MHz';
      } catch (e) {
        console.error(e);
      }
    }

    function triggerPing() {
      alert('Device is online and responding at ' + document.getElementById('ip').innerText);
    }

    async function resetWiFi() {
      if (confirm('Are you sure you want to erase saved Wi-Fi credentials?')) {
        await fetch('/reset', { method: 'POST' });
        alert('Credentials erased. Device is rebooting into setup mode...');
        window.location.reload();
      }
    }

    setInterval(updateStatus, 3000);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

void startConnectedDashboard()
{
    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    webServer.on("/", HTTP_ANY, []() {
        webServer.send_P(200, "text/html", dashboard_html);
    });

    webServer.onNotFound([]() {
        String message = "Not found: " + webServer.uri() + "\nMethod: " + String(webServer.method());
        Serial.println("[WebServer] 404 - " + message);
        webServer.send(404, "text/plain", message);
    });

    webServer.on("/api/status", HTTP_GET, []() {
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
    });

    webServer.on("/reset", HTTP_POST, []() {
        webServer.send(200, "text/plain", "Resetting...");
        delay(500);
        WiFi.disconnect(false, true);
        ESP.restart();
    });

    webServer.begin();
    Serial.print("\n[Dashboard] Web Dashboard active at http://");
    Serial.println(WiFi.localIP());
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
        startConnectedDashboard();
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
        startConnectedDashboard();
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
    if (currentState == STATE_DUAL_BROADCAST || currentState == STATE_CONNECTED)
    {
        if (currentState == STATE_DUAL_BROADCAST)
        {
            dnsServer.processNextRequest();
        }
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