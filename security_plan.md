# BLE Companion App Security — Implementation Plan

## Goal

Replace the current application-level PIN check with proper BLE pairing. After the first pairing,
the iPhone reconnects and re-encrypts automatically — no PIN entry required. The UART telemetry
service used by SeeYou Navigator is completely unaffected.

---

## How It Works Today

- A random 6-digit PIN is generated each time the Companion page opens.
- The PIN is shown on the display and must be included in every `apply` JSON command.
- There is no BLE pairing; anyone in range could attempt to send commands (blocked only by the
  PIN and the `settings_service_active` gate).

---

## How It Will Work

1. The Companion page generates a PIN and calls `NimBLEDevice::setSecurityPasskey(pin)`.
2. The companion app (browser / iPhone app) connects and attempts to write the CMD characteristic.
3. Because CMD requires authentication, NimBLE returns ATT error 0x05 (Insufficient
   Authentication). The OS intercepts this and starts the SMP pairing handshake.
4. NimBLE calls `ServerCallbacks::onPassKeyDisplay()`. This returns the PIN that is already
   shown on the device display — no separate mechanism needed.
5. The user enters the PIN into the OS pairing dialog (iOS/macOS system UI, or Chrome's
   pairing prompt on Android/desktop).
6. NimBLE completes the pairing, stores the bond in NVS, and calls
   `ServerCallbacks::onAuthenticationComplete()`.
7. The write is retried automatically by the OS and succeeds.
8. **On every subsequent connection** the OS uses the stored Long Term Key to re-encrypt the
   link silently. No PIN is shown or entered. `connInfo.isAuthenticated()` returns `true`
   immediately after the link is established.

---

## What Does Not Change

- The UART / SPP service (`6E400001-…`) and its characteristic (`6E400003-…`) keep their
  existing `READ | NOTIFY` properties with **no security flags**. Navigator never touches
  CMD or RSP, so it never receives an "Insufficient Authentication" error and is never
  prompted to pair.
- The `settings_service_active` gate remains. Even a bonded iPhone cannot write settings
  unless the Companion page is open on the device — physical intent is still required.
- The chunked notify protocol, UUIDs, JSON command format, and `get` / `apply` operations
  are unchanged.
- The `apply` response, confirmation tone, and 1-second reboot sequence are unchanged.

---

## Changes Required

### 1. `src/vario/comms/ble.cpp` — Global security configuration

In `BLE::setup()`, after `NimBLEDevice::setMTU(512)`:

```cpp
// Enable bonding with MITM protection and Secure Connections
NimBLEDevice::setSecurityAuth(true, true, true);  // bonding=true, mitm=true, sc=true
NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
// Passkey is set per-session when Companion page opens (see enableSettingsService)
```

`DISPLAY_ONLY` means: the device shows the passkey, the connecting client types it in.
This is exactly the Leaf's physical capability — it has a display but no keyboard.

### 2. `src/vario/comms/ble.cpp` — CMD and RSP characteristic properties

Change the properties when creating the settings characteristics:

```cpp
// Before:
pSettingsCmdChar = pSettingsService->createCharacteristic(
    LEAF_SETTINGS_CMD_UUID, NIMBLE_PROPERTY::WRITE);
pSettingsRspChar = pSettingsService->createCharacteristic(
    LEAF_SETTINGS_RSP_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

// After:
pSettingsCmdChar = pSettingsService->createCharacteristic(
    LEAF_SETTINGS_CMD_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN);
pSettingsRspChar = pSettingsService->createCharacteristic(
    LEAF_SETTINGS_RSP_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC |
    NIMBLE_PROPERTY::NOTIFY);
```

`WRITE_AUTHEN` requires an authenticated (passkey-verified) encrypted link to write.
`READ_ENC` requires encryption to read the RSP value (notify is not gated, but the initial
read of the characteristic is).

### 3. `src/vario/comms/ble.cpp` — ServerCallbacks security callbacks

Add three overrides to the existing `ServerCallbacks` class:

```cpp
// Called by NimBLE when it needs the passkey to send to the pairing client.
uint32_t onPassKeyDisplay() override {
    return BLE::get().getSettingsPin();
}

// Called when pairing completes (success or failure).
void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (!connInfo.isAuthenticated()) {
        // Pairing failed or was cancelled — nothing to do
        return;
    }
    // Bond is now stored in NVS. Future connections re-encrypt automatically.
    // Could notify PageBleSettings to update its status display here if needed.
}
```

### 4. `src/vario/comms/ble.cpp` — SettingsCallbacks::onWrite

Replace the application-level PIN check with the connection's authentication state:

```cpp
// Before:
uint32_t provided_pin = doc["pin"] | 0;
if (provided_pin == 0 || provided_pin != BLE::get().getSettingsPin()) {
    sendError("invalid pin");
    return;
}

// After:
if (!connInfo.isAuthenticated()) {
    sendError("not authenticated");
    return;
}
```

Remove the `"pin"` field from the `apply` JSON parsing entirely. The `apply` command
becomes:

```json
{ "op": "apply", "settings": { ... } }
```

### 5. `src/vario/comms/ble.h` — enableSettingsService signature

The PIN is still generated (it drives the passkey displayed during first pairing), but the
type can stay the same. `enableSettingsService` now also calls
`NimBLEDevice::setSecurityPasskey(pin)`:

```cpp
void BLE::enableSettingsService(uint32_t pin) {
    settings_service_active = true;
    settings_pin = pin;
    NimBLEDevice::setSecurityPasskey(pin);
}

void BLE::disableSettingsService() {
    settings_service_active = false;
    settings_pin = 0;
    NimBLEDevice::setSecurityPasskey(0);  // clear passkey when page closes
}
```

### 6. `src/vario/ui/display/pages/menu/page_menu_ble.cpp` — PageBleSettings display

The PIN is now shown as the **pairing code** rather than an "enter in web app" prompt.
Update `draw_extra()` label text:

```
// Before:
"Enter PIN in web app:"

// After:
"Pair with your device:"
```

Also useful: show a "Paired" / "Not paired" status line based on whether any bond exists.
We can check `NimBLEDevice::getNumBonds() > 0` for a simple indicator.

A "Clear pairing" action (long-press center button on the Companion page, or a dedicated
menu item) would call `NimBLEDevice::deleteBond(addr)` for each stored bond — useful for
troubleshooting.

---

## Bond Storage Notes

- NimBLE stores bonds in NVS under its own namespace. They survive device reboots
  (including the reboot after `apply`).
- Default max bonds: **3** (`CONFIG_BT_NIMBLE_MAX_BONDS` in nimconfig). Sufficient for
  a personal device. Oldest bond is silently evicted if exceeded.
- If the user factory-resets the Leaf, NVS is erased and bonds are lost. The next
  connection will require pairing again.
- The `settings_pin` member and `getSettingsPin()` remain because the PIN is still needed
  to display on-screen during first pairing. They are not used for JSON validation anymore.

---

## iOS Behaviour Notes

**The one real risk:** iOS sometimes sends a Security Request immediately after connecting,
before accessing any characteristic. If this happens while Navigator is the connecting app
(on iOS), iOS would show a pairing dialog unexpectedly.

In practice this is rare for DISPLAY_ONLY peripherals because iOS typically only initiates
pairing in response to an ATT "Insufficient Authentication" error. However, if it becomes
a problem the fallback is:

- Drop `WRITE_AUTHEN` and use only `WRITE_ENC` — this allows "Just Works" pairing
  (encrypted but no MITM/passkey). iOS handles this silently. The tradeoff is no
  protection against a nearby device spoofing the Leaf during first pairing.

**iOS RPA (rotating addresses):** iOS changes its Bluetooth address periodically for
privacy. Bonding exchanges an IRK (Identity Resolving Key) so the Leaf can always
recognise the iPhone's rotating address. NimBLE handles this automatically via
`onIdentity()`. No code change needed.

---

## Updated `client_instructions.md` Changes

Once implemented, the `apply` command loses its `pin` field:

```json
{ "op": "apply", "settings": { ... } }
```

The connection setup section gains a note: the CMD characteristic requires an authenticated
link. Web Bluetooth clients do not need to do anything special — the browser handles the
pairing dialog automatically when the first write fails with "Insufficient Authentication".

---

## Files to Modify

| File | Change |
|---|---|
| `src/vario/comms/ble.cpp` | Global security config; CMD/RSP property flags; `ServerCallbacks` security overrides; remove PIN check from `SettingsCallbacks::onWrite`; update `enableSettingsService` / `disableSettingsService` |
| `src/vario/comms/ble.h` | No signature changes needed |
| `src/vario/ui/display/pages/menu/page_menu_ble.cpp` | Update `draw_extra()` label; optionally show bond count / "Clear pairing" action |
| `client_instructions.md` | Remove `pin` field from `apply` example; add pairing note to connection setup |

---

## Implementation Order

1. Global security config + characteristic flags (compile-check first)
2. `ServerCallbacks` security overrides
3. `SettingsCallbacks` — swap PIN check for `isAuthenticated()`
4. `enableSettingsService` / `disableSettingsService` — call `setSecurityPasskey`
5. `PageBleSettings` display text update
6. Build and test with `leaf_3_2_7_dev`
7. Update `client_instructions.md`
