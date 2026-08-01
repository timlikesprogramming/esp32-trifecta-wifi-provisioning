# ESP32 Trifecta Wi-Fi Provisioning

A complete, modular, commercial-grade Wi-Fi provisioning solution for the ESP32 (specifically optimized for the ESP32-S3).

This project uses a "Trifecta" fallback architecture to ensure users can *always* connect your device to their network, regardless of their technical skill or device support:
1. **Web Bluetooth (Improv BLE):** The primary seamless setup flow directly from a web browser.
2. **SoftAP Captive Portal:** An automatic fallback if the user lacks Bluetooth or is using an incompatible browser.
3. **WPS (Wi-Fi Protected Setup):** A hardware fallback activated by a short press on the physical BOOT button for headless setup.

Once connected, the ESP32 serves a dashboard interface and broadcasts its address via **mDNS** so you can easily access it without searching for its IP address.

---

## 🚀 Features
- **Modular C++ Backend:** Cleanly separates hardware inputs, web portal routing, and Bluetooth GATT servers.
- **Zero-Configuration Networking:** Automatically broadcasts `http://mydevice.local` via mDNS on your home network.
- **Resilience:** Features exponential backoff for router reboots and credential-erasing factory resets (hold BOOT button for 5s).
- **Modern UI:** The frontend (`web-setup/`) is built with modern, responsive HTML/CSS/JS.

---

## 🛠️ Testing the Web UI Locally

The frontend interacts with your ESP32 over Web Bluetooth. Because Web Bluetooth requires a Secure Context, you must either serve it over HTTPS or test it from `localhost`.

To run the web interface locally for testing, use Python's built-in HTTP server:

```bash
cd web-setup
python3 -m http.server 8080
```
Then open your browser to `http://localhost:8080`.

---

## 🌐 Enabling Web Bluetooth (Linux Users)

Web Bluetooth is widely supported on Android, Chrome OS, and macOS natively via Google Chrome and Edge.

However, if you are developing or testing on **Linux**, Web Bluetooth is often disabled by default.

### For Chrome / Chromium / Brave / Edge on Linux:
1. Type `chrome://flags` into your URL bar and hit Enter.
2. Search for **"Experimental Web Platform features"** (`#enable-experimental-web-platform-features`).
3. Set it to **Enabled**.
4. Search for **"Use the new permissions backend for Web Bluetooth"** (if present) and enable it.
5. Relaunch your browser.

*Note: Make sure your Linux Bluetooth daemon (`bluetoothd`) is running and your computer's Bluetooth adapter is turned on.*

### For Firefox:
Mozilla Firefox **does not** currently officially support the Web Bluetooth API on any platform. While there is an experimental flag (`dom.bluetooth.enabled` in `about:config`), it is highly unstable and not recommended. Please use a Chromium-based browser for provisioning.

---

## ⚡ Flashing the Firmware

This project is built with PlatformIO.

1. Open the project in VS Code with the PlatformIO extension installed.
2. Connect your ESP32-S3 via USB.
3. Build and Upload:
```bash
pio run -t upload
```

Once flashed, the device will wait 10 seconds for a saved Wi-Fi connection. If it fails, it will begin broadcasting both its Improv BLE signal and a `MyDevice_Setup` Wi-Fi network.

Enjoy your seamless provisioning experience!
