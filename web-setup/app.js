const SVC_UUID_128       = "00467768-6228-2272-4663-277478268000";
const SVC_UUID_16        = 0x4677;
const CHAR_STATE_UUID    = "00467768-6228-2272-4663-277478268001";
const CHAR_ERROR_UUID    = "00467768-6228-2272-4663-277478268002";
const CHAR_RPC_CMD_UUID  = "00467768-6228-2272-4663-277478268003";
const CHAR_RPC_RES_UUID  = "00467768-6228-2272-4663-277478268004";

let gattDevice = null;
let rpcCmdChar = null;
let rpcResChar = null;
let scannedSSIDs = [];

// Check Web Bluetooth support on load
document.addEventListener("DOMContentLoaded", () => {
    if (!("bluetooth" in navigator) || !window.isSecureContext) {
        document.getElementById("connectBtBtn").style.display = "none";
        document.getElementById("unsupportedNotice").style.display = "block";
    }
    
    // Bind form submission
    const form = document.getElementById("wifiForm");
    if (form) form.addEventListener("submit", submitCredentials);
});

async function startBLEProvisioning() {
    try {
        document.getElementById("connectBtBtn").disabled = true;
        document.getElementById("connectBtBtn").innerText = "Pairing Bluetooth...";

        gattDevice = await navigator.bluetooth.requestDevice({
            filters: [
                { services: [SVC_UUID_16] },
                { services: [SVC_UUID_128] },
                { namePrefix: "MyProduct" },
                { namePrefix: "MyDevice" }
            ],
            optionalServices: [SVC_UUID_128, SVC_UUID_16]
        });

        const server = await gattDevice.gatt.connect();
        let service;
        try {
            service = await server.getPrimaryService(SVC_UUID_128);
        } catch (e) {
            service = await server.getPrimaryService(SVC_UUID_16);
        }

        rpcCmdChar = await service.getCharacteristic(CHAR_RPC_CMD_UUID);
        rpcResChar = await service.getCharacteristic(CHAR_RPC_RES_UUID);
        const stateChar = await service.getCharacteristic(CHAR_STATE_UUID);
        const errorChar = await service.getCharacteristic(CHAR_ERROR_UUID);

        await rpcResChar.startNotifications();
        rpcResChar.addEventListener('characteristicvaluechanged', handleRPCNotification);

        await stateChar.startNotifications();
        stateChar.addEventListener('characteristicvaluechanged', (e) => {
            const state = e.target.value.getUint8(0);
            console.log(`[BLE State Changed] State: ${state}`);
            if (state === 0x03) { // Provisioning
                document.getElementById("submitBtn").innerText = "Connecting to Wi-Fi...";
            }
        });

        await errorChar.startNotifications();
        errorChar.addEventListener('characteristicvaluechanged', (e) => {
            const err = e.target.value.getUint8(0);
            console.log(`[BLE Error Changed] Error: ${err}`);
            if (err !== 0x00) {
                document.getElementById("submitBtn").disabled = false;
                document.getElementById("submitBtn").innerText = "Connect Device";
                alert(`Provisioning Error Code: ${err}`);
            }
        });

        gattDevice.addEventListener('gattserverdisconnected', () => {
            console.log("BLE Disconnected unexpectedly.");
            document.getElementById("submitBtn").disabled = false;
            document.getElementById("submitBtn").innerText = "Connect Device";
            document.getElementById("bleModal").style.display = "none";
            document.getElementById("btContainer").style.display = "block";
            alert("Bluetooth connection lost! The device may have restarted or the signal dropped.");
        });

        document.getElementById("btContainer").style.display = "none";
        document.getElementById("bleModal").style.display = "block";

        // Send COMMAND_SCAN (0x04) over BLE
        requestWiFiScan();

    } catch (err) {
        console.error("BLE Connect Error:", err);
        document.getElementById("connectBtBtn").disabled = false;
        document.getElementById("connectBtBtn").innerText = "Connect Device via Bluetooth";
    }
}

async function writeRPC(char, packet) {
    console.log("[BLE Write RPC]", packet);
    try {
        if (char.properties.writeWithoutResponse && char.writeValueWithoutResponse) {
            await char.writeValueWithoutResponse(packet);
        } else if (char.writeValueWithResponse) {
            await char.writeValueWithResponse(packet);
        } else {
            await char.writeValue(packet);
        }
    } catch (err) {
        console.error("[BLE Write Error]", err);
    }
}

async function requestWiFiScan() {
    scannedSSIDs = [];
    document.getElementById("scanStatus").innerHTML = '<span class="spinner"></span> Scanning nearby 2.4GHz Wi-Fi networks...';
    document.getElementById("ssidSelect").innerHTML = '<option value="" disabled selected>Scanning nearby networks...</option>';
    document.getElementById("submitBtn").disabled = true;

    // Packet format: [Cmd=0x04, Len=0x00, Checksum=0x04]
    const scanPacket = new Uint8Array([0x04, 0x00, 0x04]);
    await writeRPC(rpcCmdChar, scanPacket);
}

function handleRPCNotification(event) {
    try {
        const data = event.target.value; // DataView
        const cmd = data.getUint8(0);
        const dataLen = data.getUint8(1);

        console.log(`[BLE Notification] Received Cmd: 0x${cmd.toString(16)}, DataLen: ${dataLen}, Total Bytes: ${data.byteLength}`);

        if (cmd === 0x04) {
            if (dataLen === 0) {
                // Scan complete
                console.log("[BLE Scan] Scan completion signal (0x04, 0x00) received!");
                populateDropdown();
                document.getElementById("scanStatus").innerHTML = '✔ Wi-Fi scan complete. Select your network below:';
                return;
            }

            // Parse compact binary payload:
            // [Cmd (1)] [Len (1)] [SSID_Len (1)] [SSID (SSID_Len)] [RSSI (1)] [Auth (1)] [Checksum (1)]
            // The `dataLen` from the header should equal `1 + SSID_Len + 1 + 1`.
            if (data.byteLength < 5) return; // Malformed

            const ssidLen = data.getUint8(2);
            let ssid = "";
            for (let i = 0; i < ssidLen; i++) {
                ssid += String.fromCharCode(data.getUint8(3 + i));
            }

            const rssi = data.getInt8(3 + ssidLen); // signed 8-bit integer
            const authByte = data.getUint8(3 + ssidLen + 1);
            const auth = (authByte === 0) ? "NO" : "YES";

            console.log(`[BLE Scan] Parsed -> SSID: ${ssid}, RSSI: ${rssi}, Auth: ${auth}`);

            // Append to scanned networks list
            const signalStr = (rssi >= -60) ? "Strong" : (rssi >= -75) ? "Good" : "Weak";
            scannedSSIDs.push({
                ssid: ssid,
                label: `${ssid} (${rssi} dBm - ${signalStr})`
            });
        }
        else if (cmd === 0x01) {
            // Provisioning result payload format:
            // [Cmd=0x01] [Len] [URL_Len] [URL] [Checksum]
            if (dataLen > 0) {
                const urlLen = data.getUint8(2);
                let url = "";
                for (let i = 0; i < urlLen; i++) {
                    url += String.fromCharCode(data.getUint8(3 + i));
                }
                console.log(`[BLE Provision] Success! Device IP URL: ${url}`);
                
                document.getElementById("wifiForm").style.display = "none";
                document.getElementById("statusMessage").style.display = "block";
                document.getElementById("statusMessage").innerHTML = `
                    <h2 style="color: #22c55e;">Credentials Received!</h2>
                    <p style="color: #94a3b8; font-size: 0.9rem;">The device has successfully connected to your network.</p>
                    <p style="color: #94a3b8; font-size: 0.9rem;">You can now manage your device here:</p>
                    <a href="${url}" target="_blank" style="display:inline-block; margin-top:10px; background:#22c55e; color:#0f172a; padding:10px 20px; border-radius:8px; text-decoration:none; font-weight:bold;">Open Device Dashboard</a>
                `;
            }
        }
    } catch (err) {
        console.error("[BLE Notification Error]", err);
        document.getElementById("scanStatus").innerHTML = "❌ Error parsing scan result: " + err.message;
    }
}

function populateDropdown() {
    const select = document.getElementById("ssidSelect");
    select.innerHTML = '<option value="" disabled selected>Select a network</option>';
    
    // Sort by RSSI or alphabetically if no RSSI is available
    scannedSSIDs.forEach(net => {
        const opt = document.createElement("option");
        opt.value = net.ssid;
        opt.innerText = net.label;
        select.appendChild(opt);
    });

    document.getElementById("submitBtn").disabled = false;
}

function toggleManualSSID() {
    const sel = document.getElementById('ssidSelect');
    const inp = document.getElementById('ssidInput');
    if (inp.style.display === 'none') {
        inp.style.display = 'block';
        inp.disabled = false;
        sel.style.display = 'none';
        sel.disabled = true;
    } else {
        inp.style.display = 'none';
        inp.disabled = true;
        sel.style.display = 'block';
        sel.disabled = false;
    }
}

async function identifyDevice() {
    // Send RPC Command 0x02 (Identify)
    const packet = new Uint8Array([0x02, 0x00, 0x02]);
    await writeRPC(rpcCmdChar, packet);
}

function disconnectDevice() {
    if (gattDevice && gattDevice.gatt.connected) {
        gattDevice.gatt.disconnect();
    }
    // Reset UI
    document.getElementById("bleModal").style.display = "none";
    document.getElementById("btContainer").style.display = "block";
    document.getElementById("connectBtBtn").disabled = false;
    document.getElementById("connectBtBtn").innerText = "Connect Device via Bluetooth";
}

async function submitCredentials(event) {
    event.preventDefault();

    const isManual = document.getElementById('ssidInput').style.display !== 'none';
    const ssid = isManual ? document.getElementById('ssidInput').value : document.getElementById('ssidSelect').value;
    const pass = document.getElementById('wifiPassword').value;

    document.getElementById("submitBtn").disabled = true;
    document.getElementById("submitBtn").innerText = "Sending credentials...";

    const ssidBytes = new TextEncoder().encode(ssid);
    const passBytes = new TextEncoder().encode(pass);

    // Payload: ssid_len (1) + ssid + pass_len (1) + pass
    const payloadLen = 1 + ssidBytes.length + 1 + passBytes.length;
    const packet = new Uint8Array(3 + payloadLen);
    
    packet[0] = 0x01; // Command: Send Wi-Fi
    packet[1] = payloadLen;
    packet[2] = ssidBytes.length;
    packet.set(ssidBytes, 3);
    packet[3 + ssidBytes.length] = passBytes.length;
    packet.set(passBytes, 4 + ssidBytes.length);

    let checksum = 0;
    for (let i = 0; i < packet.length - 1; i++) {
        checksum += packet[i];
    }
    packet[packet.length - 1] = checksum & 0xFF;

    await writeRPC(rpcCmdChar, packet);
}
