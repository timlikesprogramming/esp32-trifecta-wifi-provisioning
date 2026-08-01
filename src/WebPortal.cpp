#include "WebPortal.h"
#include "HtmlTemplates.h"
#include <WiFi.h>

WebPortal::WebPortal() : webServer(80), isCaptivePortal(false) {
    // Register unified routes once
    webServer.on("/", HTTP_ANY, [this]() {
        if (isCaptivePortal) {
            if (webServer.method() == HTTP_GET) {
                handlePortalRoot();
            } else {
                webServer.send(405, "text/plain", "Method Not Allowed");
            }
        } else {
            webServer.send_P(200, "text/html", dashboard_html);
        }
    });

    webServer.on("/connect", HTTP_POST, [this]() {
        if (isCaptivePortal) {
            handlePortalConnect();
        } else {
            webServer.send(404, "text/plain", "Not found");
        }
    });

    webServer.on("/api/status", HTTP_GET, [this]() {
        if (!isCaptivePortal) {
            handleStatusApi();
        } else {
            webServer.send(404, "text/plain", "Not found");
        }
    });

    webServer.on("/reset", HTTP_POST, [this]() {
        if (!isCaptivePortal) {
            webServer.send(200, "text/plain", "Resetting...");
            if (resetCallback) resetCallback();
        } else {
            webServer.send(404, "text/plain", "Not found");
        }
    });

    // Captive Portal OS triggers
    auto redirectRoot = [this]() {
        if (isCaptivePortal) {
            handlePortalRoot();
        } else {
            webServer.send(404, "text/plain", "Not found");
        }
    };
    webServer.on("/hotspot-detect.html", redirectRoot);
    webServer.on("/generate_204", redirectRoot);
    webServer.on("/connecttest.txt", redirectRoot);
    webServer.on("/ncsi.txt", redirectRoot);

    webServer.onNotFound([this]() {
        if (isCaptivePortal) {
            webServer.sendHeader("Location", String("http://192.168.4.1"), true);
            webServer.send(302, "text/plain", "");
        } else {
            String message = "Not found: " + webServer.uri();
            webServer.send(404, "text/plain", message);
        }
    });
}

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
    webServer.begin();
}

void WebPortal::startDashboard() {
    stop();
    isCaptivePortal = false;
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
