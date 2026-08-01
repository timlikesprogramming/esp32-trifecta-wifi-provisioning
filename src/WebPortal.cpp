#include "WebPortal.h"
#include "HtmlTemplates.h"
#include <WiFi.h>

WebPortal::WebPortal() : webServer(80), isCaptivePortal(false) {}

void WebPortal::onCredentialsReceived(std::function<void(String ssid, String pass)> callback) {
    credsCallback = callback;
}

void WebPortal::onResetRequested(std::function<void()> callback) {
    resetCallback = callback;
}

void WebPortal::startCaptivePortal(const char* apSsid, IPAddress apIp, byte dnsPort) {
    stop();
    isCaptivePortal = true;
    
    WiFi.softAP(apSsid);
    dnsServer.start(dnsPort, "*", apIp);

    webServer.on("/", HTTP_GET, std::bind(&WebPortal::handlePortalRoot, this));
    webServer.on("/connect", HTTP_POST, std::bind(&WebPortal::handlePortalConnect, this));

    webServer.on("/hotspot-detect.html", std::bind(&WebPortal::handlePortalRoot, this));
    webServer.on("/generate_204", std::bind(&WebPortal::handlePortalRoot, this));
    webServer.on("/connecttest.txt", std::bind(&WebPortal::handlePortalRoot, this));
    webServer.on("/ncsi.txt", std::bind(&WebPortal::handlePortalRoot, this));

    webServer.onNotFound([this, apIp]() {
        webServer.sendHeader("Location", String("http://") + apIp.toString(), true);
        webServer.send(302, "text/plain", "");
    });
    
    webServer.begin();
}

void WebPortal::startDashboard() {
    stop();
    isCaptivePortal = false;

    webServer.on("/", HTTP_ANY, [this]() {
        webServer.send_P(200, "text/html", dashboard_html);
    });

    webServer.onNotFound([this]() {
        String message = "Not found: " + webServer.uri();
        webServer.send(404, "text/plain", message);
    });

    webServer.on("/api/status", HTTP_GET, [this]() {
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

    webServer.on("/reset", HTTP_POST, [this]() {
        webServer.send(200, "text/plain", "Resetting...");
        if (resetCallback) resetCallback();
    });

    webServer.begin();
}

void WebPortal::stop() {
    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
}

void WebPortal::loop() {
    if (isCaptivePortal) {
        dnsServer.processNextRequest();
    }
    webServer.handleClient();
}

void WebPortal::handlePortalRoot() {
    Serial.println("\n[Captive Portal] Client requested portal. Scanning Wi-Fi networks...");
    int n = WiFi.scanNetworks(false, true);
    String formFields = "";

    if (n > 0) {
        std::vector<String> seenSSIDs;
        String options = "";
        for (int i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;

            bool duplicate = false;
            for (const auto &s : seenSSIDs) {
                if (s == ssid) { duplicate = true; break; }
            }

            if (!duplicate) {
                seenSSIDs.push_back(ssid);
                int32_t rssi = WiFi.RSSI(i);
                String signal = (rssi >= -60) ? "Strong" : (rssi >= -75) ? "Good" : "Weak";
                options += "<option value=\"" + ssid + "\">" + ssid + " (" + String(rssi) + " dBm - " + signal + ")</option>\n";
            }
        }

        if (seenSSIDs.size() > 0) {
            formFields += "<label for=\"ssidSelect\">Wi-Fi Network</label>\n";
            formFields += "<select id=\"ssidSelect\" name=\"ssid\">\n" + options + "</select>\n";
            formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" style=\"display:none;\" disabled required>\n";
            formFields += "<button type=\"button\" id=\"manualBtn\" class=\"manual-btn\" onclick=\"toggleManual()\">Enter hidden SSID manually</button>\n";
        } else {
            formFields += "<label for=\"ssidInput\">Wi-Fi Network</label>\n";
            formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" required>\n";
        }
    } else {
        formFields += "<label for=\"ssidInput\">Wi-Fi Network</label>\n";
        formFields += "<input type=\"text\" id=\"ssidInput\" name=\"ssid\" placeholder=\"Enter Wi-Fi Network Name\" required>\n";
    }
    WiFi.scanDelete();

    String fullHTML = String(portal_header_html) + formFields + String(portal_footer_html);
    webServer.send(200, "text/html", fullHTML);
}

void WebPortal::handlePortalConnect() {
    String ssid = webServer.arg("ssid");
    String pass = webServer.arg("pass");

    webServer.send_P(200, "text/html", success_html);
    
    if (credsCallback) {
        credsCallback(ssid, pass);
    }
}
