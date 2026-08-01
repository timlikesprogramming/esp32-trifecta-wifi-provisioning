#pragma once
#include <Arduino.h>

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
