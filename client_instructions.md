# Leaf Vario — BLE Settings Client Instructions

Everything needed to build a Web Bluetooth (or native BLE) client that reads and writes all
device settings on a Leaf vario.

---

## 1. Prerequisites on the Device

The settings service is **opt-in** and only active while the *BT Settings* page is open on the
device display. The page is reached via:

```
System Menu → Bluetooth → Settings
```

While that page is open:
- A random 6-digit PIN is shown on the display. It changes every time the page is opened.
- The settings GATT service accepts commands.
- Navigating away from the page (back button or power-off) deactivates the service and invalidates
  the PIN.

Bluetooth must be turned **on** before entering the Settings sub-page (the menu enforces this).

---

## 2. BLE Discovery

### Device name
```
Leaf: <FANET_ADDRESS>
```
Example: `Leaf: FB5F20`. Filter on the device name prefix `"Leaf: "` or on the settings
service UUID to find the correct device.

### Service UUIDs advertised
| Purpose | UUID |
|---|---|
| UART / telemetry (SeeYou Navigator) | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| **Settings service** | `0A29AB06-0F01-4B27-AE93-1E86D81B9784` |

Both UUIDs appear in the advertising packet's service list.

### Settings service characteristics
| Role | UUID | Properties |
|---|---|---|
| **CMD** — client sends commands | `42433AC8-814C-4E2F-AD52-C8F6CBCC96D7` | WRITE |
| **RSP** — device sends responses | `A621D682-D621-43D5-A1BD-4AF1F3F5271F` | READ, NOTIFY |

---

## 3. Connection Setup

1. Connect to the device.
2. **Subscribe to notifications** on the RSP characteristic before sending any command. All
   responses (including errors) arrive as notifications.
3. The device negotiates MTU 512. Most stacks handle this automatically; if yours does not,
   request MTU 512 immediately after connecting.

### Web Bluetooth snippet
```js
const device = await navigator.bluetooth.requestDevice({
  filters: [{ namePrefix: 'Leaf: ' }],
  optionalServices: ['0a29ab06-0f01-4b27-ae93-1e86d81b9784'],
});

const server  = await device.gatt.connect();
const service = await server.getPrimaryService('0a29ab06-0f01-4b27-ae93-1e86d81b9784');

const cmdChar = await service.getCharacteristic('42433ac8-814c-4e2f-ad52-c8f6cbcc96d7');
const rspChar = await service.getCharacteristic('a621d682-d621-43d5-a1bd-4af1f3f5271f');

await rspChar.startNotifications();
```

---

## 4. Chunked Notification Protocol

Responses longer than ~508 bytes (the full settings JSON is ~1.8 KB) are split into chunks.
Every notification — including single-chunk error responses — carries a **1-byte prefix**:

| Prefix byte | Meaning |
|---|---|
| `0x00` | More chunks follow — append payload to buffer |
| `0xFF` | Final (or only) chunk — parse accumulated buffer |

The payload immediately follows the prefix byte with no other framing.

### Reassembly
```js
let buffer = '';

rspChar.addEventListener('characteristicvaluechanged', (event) => {
  const data  = event.target.value;           // DataView
  const flag  = data.getUint8(0);
  const chunk = new TextDecoder().decode(data.buffer.slice(1));

  buffer += chunk;

  if (flag === 0xFF) {
    const response = JSON.parse(buffer);
    buffer = '';
    handleResponse(response);
  }
});
```

> **Note:** Error responses from the device are always a single notification (`0xFF` prefix) and
> are valid JSON, so the same reassembly path handles them.

---

## 5. Commands

All commands are JSON objects written to the CMD characteristic as UTF-8 bytes. Commands fit
within a single write (no chunking required on the client side).

### 5.1 Get all settings

**Request**
```json
{"op":"get"}
```

**Response** — chunked, reassemble then parse:
```json
{
  "vario_sinkAlarm": -1.6,
  "vario_sinkAlarm_units": false,
  ...
}
```
See [Section 7](#7-settings-reference) for the full field list.

### 5.2 Apply settings

**Request**
```json
{
  "op": "apply",
  "pin": 123456,
  "settings": {
    "igc_pilotName": "Jane Doe",
    "vario_volume": 2
  }
}
```

- `pin` — the 6-digit integer shown on the device display. Required; the device rejects the
  command with `"invalid pin"` if it is missing or wrong.
- `settings` — an object containing **only the fields you want to change**. Partial updates are
  supported; omitted fields are left unchanged.
- `macAddress` is read-only and silently ignored if included.

**Success response** (single notification, `0xFF` prefix)
```json
{"ok": true}
```

After sending this response the device:
1. Saves all settings to non-volatile storage.
2. Plays an audible confirmation tone.
3. Reboots after ~1 second.

**Error response** (single notification, `0xFF` prefix)
```json
{"ok": false, "error": "<reason>"}
```

| `error` value | Cause |
|---|---|
| `"settings service not active"` | BT Settings page is not open on the device |
| `"invalid json"` | CMD write was not valid JSON |
| `"invalid pin"` | `pin` field missing, zero, or doesn't match the on-screen PIN |
| `"invalid settings"` | `settings` value is not a JSON object |
| `"unknown op"` | `op` field not recognised |

### Write helper
```js
async function sendCommand(cmdChar, obj) {
  const bytes = new TextEncoder().encode(JSON.stringify(obj));
  await cmdChar.writeValueWithoutResponse(bytes);
}
```

---

## 6. Full Flow Example

```js
// --- 1. Get current settings ---
let settingsJson = null;

function handleResponse(response) {
  if (response.ok === false) {
    console.error('Device error:', response.error);
    return;
  }
  // If it has a known settings key it's a get response; otherwise it's an apply ack
  if ('vario_volume' in response) {
    settingsJson = response;
    console.log('Settings received:', settingsJson);
  } else {
    console.log('Apply acknowledged — device is rebooting');
  }
}

await sendCommand(cmdChar, { op: 'get' });

// --- 2. Mutate and apply ---
// (After settingsJson is populated by the notification handler above)
const pin = parseInt(document.getElementById('pin-input').value, 10);

await sendCommand(cmdChar, {
  op: 'apply',
  pin: pin,
  settings: {
    igc_pilotName:  'Jane Doe',
    igc_gliderType: 'Ozone Enzo 3',
    igc_gliderId:   'G-JANE',
    units_alt:      true,   // feet
    units_climb:    true,   // fpm
    vario_volume:   2,
  },
});
```

---

## 7. Settings Reference

All fields present in the `get` response. All fields (except `macAddress`) are writable via
`apply`. Type annotations: `float`, `int`, `bool`, `string`.

### Vario

| Key | Type | Range / Notes |
|---|---|---|
| `vario_sinkAlarm` | float | Sink alarm threshold. Valid m/s values: `0, -1.2, -1.4, -1.6, -1.8, -2.0, -2.5, -3.0, -4.0, -5.0, -6.0`. Valid fpm values: `0, -240, -280, -320, -360, -400, -500, -600, -800, -1000, -1200`. `0` = off. |
| `vario_sinkAlarm_units` | bool | `false` = m/s, `true` = fpm. Change this before changing `vario_sinkAlarm` when switching unit systems. |
| `vario_sensitivity` | int | 1–5. 1 = slowest (1 s moving average), 5 = instant (single sample). |
| `vario_climbAvg` | int | 0–3 (units of 5 seconds; 0 = 0 s, 3 = 15 s). |
| `vario_climbStart` | int | 0–20 cm/s. Climb rate at which the climb tone begins. |
| `vario_volume` | int | 0–3 (0 = mute, 3 = high). |
| `vario_quietMode` | bool | `true` = suppress tones until flight recording starts. |
| `vario_tones` | bool | `false` = linear pitch interpolation, `true` = musical scale (major/minor). |
| `vario_liftyAir` | int | −8 to 0 (units of 0.1 m/s). `0` = feature off. Triggers a gentle tone when sink is less than this threshold but above `vario_climbStart`. |
| `vario_altSetting` | float | Altimeter QNH in inHg. Default `29.921`. |
| `vario_altSyncToGPS` | bool | Lock barometric altimeter to GPS altitude. |

### GPS & Track Log

| Key | Type | Range / Notes |
|---|---|---|
| `distanceFlownType` | bool | `false` = XC (straight-line) distance, `true` = path (odometer) distance. |
| `gpsMode` | int | `0` = off, `1` = on, `2` = power-save. |
| `log_saveTrack` | bool | Enable track log recording. |
| `log_autoStart` | bool | Start recording automatically when airborne. |
| `log_autoStop` | bool | Stop recording automatically when landed. |
| `log_format` | int | `0` = IGC, `1` = KML. |

### IGC Pilot & Glider Info

These fields are written into the IGC file header on every recorded flight.

| Key | Type | IGC Header | Notes |
|---|---|---|---|
| `igc_pilotName` | string | `HFPLT` | Pilot's full name. |
| `igc_gliderType` | string | `HFGTY` | Glider model (e.g. `"Ozone Enzo 3"`). |
| `igc_gliderId` | string | `HFGID` | Glider registration or serial number. |
| `igc_competitionId` | string | `HFCID` | Competition ID / pilot number. |
| `igc_competitionClass` | string | `HFCCL` | Competition class (e.g. `"Open"`). |

Empty strings are stored and written as the IGC default `"Unknown"` for `pilotName` /
`gliderType`; the other three fields are omitted from the IGC header when empty.

### System

| Key | Type | Range / Notes |
|---|---|---|
| `system_timeZone` | int | −720 to 840 **minutes** from UTC. Examples: UTC−8 = −480, UTC+5:30 = 330. |
| `system_volume` | int | 0–3 (system/UI sounds). |
| `system_ecoMode` | bool | Eco (low-power) mode. |
| `system_autoOff` | int | Auto power-off in minutes. Valid values: `0` (disabled), `1, 5, 10, 15, 30, 45, 60`. |
| `system_wifiOn` | bool | WiFi enabled at boot. |
| `system_bluetoothOn` | bool | Bluetooth enabled at boot. |
| `system_showWarning` | bool | Show safety warning on startup. |

### Display

| Key | Type | Range / Notes |
|---|---|---|
| `disp_contrast` | int | 1–20. |
| `disp_navPageAltType` | int | Altitude source on Nav page (0–2). |
| `disp_thmPageAltType` | int | Primary altitude source on Thermal page. |
| `disp_thmPageAlt2Type` | int | Secondary altitude source on Thermal page. |
| `disp_thmPageUser1` | int | User field 1 on Thermal page. |
| `disp_thmPageUser2` | int | User field 2 on Thermal page. |
| `disp_showDebugPage` | bool | Show debug data page. |
| `disp_showSimplePage` | bool | Show simple display page. |
| `disp_showThmPage` | bool | Show thermal page. |
| `disp_showThmAdvPage` | bool | Show advanced thermal page. |
| `disp_showNavPage` | bool | Show navigation page. |
| `startPage` | int | Page index shown at boot. |

### FANET

| Key | Type | Notes |
|---|---|---|
| `fanet_region` | int | `0` = off. Regional code for FANET frequency band. |
| `fanet_address` | string | FANET device address (e.g. `"FB5F20"`). Typically set during production; change with care. |

### Units

All are `bool`. `false` = metric / 24-hour. `true` = imperial / 12-hour.

| Key | `false` | `true` |
|---|---|---|
| `units_climb` | m/s | fpm |
| `units_alt` | metres | feet |
| `units_temp` | °C | °F |
| `units_speed` | km/h | mph |
| `units_heading` | degrees (e.g. 342°) | cardinal (e.g. NNW) |
| `units_distance` | km / m | miles / ft |
| `units_hours` | 24-hour | 12-hour |

### Read-only

| Key | Type | Notes |
|---|---|---|
| `macAddress` | string | Device WiFi MAC address (e.g. `"A4:CF:12:34:56:78"`). Present in `get` response; silently ignored in `apply`. |

---

## 8. Behaviour After Apply

On a successful `apply` the device:

1. Returns `{"ok":true}` via a single notification.
2. Persists all settings to flash (ESP32 NVS).
3. Plays an audible confirmation tone (~1 second duration).
4. **Reboots** approximately 1 second after the notification.

The BLE connection will drop when the device reboots. Your app should handle the disconnect
gracefully (e.g. show a "Device is restarting…" message and attempt to reconnect after a few
seconds).

Bluetooth will be available again once the device finishes rebooting, assuming
`system_bluetoothOn` was `true` in the applied settings (or was already `true`).

---

## 9. Security Notes

- The PIN is **application-level only** — no BLE pairing or bonding is required.
- The PIN is a random 6-digit integer (100000–999999) generated fresh each time the BT Settings
  page is opened.
- The settings service is completely inactive (ignores all writes) when the BT Settings page is
  not open, so the attack surface is limited to the window when a user has deliberately navigated
  to that page.
- The existing UART telemetry service (used by SeeYou Navigator) is separate and unaffected by
  the settings service state.
