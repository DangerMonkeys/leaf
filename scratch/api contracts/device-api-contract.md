# Leaf Log — device API contract (for the Leaf firmware)

This is the **contract the Leaf firmware consumes** to pair with a pilot's Leaf Log
account and auto-upload IGC flights. The Leaf Log side is implemented and live; this
document is the source of truth for the firmware implementation.

- **Base URL:** `https://leaflog.norcalflight.com`
- **Transport:** HTTPS, directly to the base URL. No reverse proxy, no plain HTTP.
- **Auth:** a scoped, revocable **bearer token** the device obtains via pairing.
- **All three device endpoints are `POST`** and are safe to call from the device
  (no browser session needed).

The device never logs in and never sees a password. The only human step is the pilot
typing a short **pairing code** into the Leaf Log website — the firmware does **not**
implement that step.

---

## The flow

```
 firmware                         Leaf Log                         pilot (browser, HTTPS)
 ────────                         ────────                         ─────────────────────
 POST /api/devices/pair/start ──►  mint code + pollHandle
   ◄── { code, pollHandle, expiresAt }
 surface the ACTIVATION URL   ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─►  opens
   https://…/activate?code=<code>                                  /activate?code=…,
   (as a QR and/or shown link)                                     signs in, one tap
                                  claim: bind to pilot,  ◄─────────  (session-authed)
                                  mint device token
 POST /api/devices/pair/poll ──►  (poll every few s the whole time)
   ◄── { status: "pending" }        …until claimed…
 POST /api/devices/pair/poll ──►
   ◄── { status: "claimed", token: "llk_…" }   ← delivered ONCE
 store token persistently, discard pollHandle

 later, per recorded flight:
 POST /api/ingest  (Bearer token, raw IGC body) ──►  ingest + dedupe
   ◄── { flightId, status, deduped }
```

---

## 1) `POST /api/devices/pair/start`

Begin pairing. Call once when the pilot starts the "connect to Leaf Log" action.

- **Request:** no headers or body required.
- **Response `200`:**
  ```json
  { "code": "B2DUTQ", "pollHandle": "OMlAjdOS…(opaque)…", "expiresAt": "2026-07-01T10:55:49.914Z" }
  ```
  | field        | type   | notes |
  |--------------|--------|-------|
  | `code`       | string | 6 chars, shown to the pilot (see [code format](#formats)). |
  | `pollHandle` | string | opaque; the device keeps it to poll. **Not** shown to the pilot. |
  | `expiresAt`  | string | ISO-8601; the pairing is valid for **10 minutes**. |
- **`500`** `{ "error": … }` — transient; retry.

Firmware: keep `pollHandle` and `expiresAt` in memory, and surface the activation URL
(next section). Then start polling (section after that) — poll continuously while the
pilot activates.

## Activation URL (required)

After `pair/start`, the device must surface this URL so the pilot can activate on their
phone/computer:

```
https://leaflog.norcalflight.com/activate?code=<code>
```

- `<code>` is the `code` from `pair/start` (URL-safe as-is; don't add the cosmetic dash).
- **How you present it is up to the firmware** — a scannable **QR code** is the smoothest
  (scan → opens the link), but a shown link/text works too. The contract only requires
  that the pilot can reach `…/activate?code=<code>`.
- Opening it takes the pilot to a "Connect your Leaf" page: they sign in if needed, then
  **one tap** claims the device. (Fallback: they can instead type the raw `code` under
  **Settings → Devices** — so showing the code as text alongside is a nice backup.)
- Activation still requires the pilot to be **logged in** — the URL only replaces typing
  the code; it does not weaken auth.

The device does not call `/activate` — it just displays the URL and keeps polling.

## 2) `POST /api/devices/pair/poll`

Poll until the pilot has claimed the code, then receive the token **once**.

- **Request:** `Content-Type: application/json`, body:
  ```json
  { "pollHandle": "…the pollHandle from start…" }
  ```
- **Response `200`:** `{ "status": <status>, "token"?: "llk_…" }`
  | `status`   | meaning | firmware action |
  |------------|---------|-----------------|
  | `pending`  | not claimed yet | keep polling until `expiresAt`. |
  | `claimed`  | claimed — **`token` is present** | **store the token now**, stop polling, discard `pollHandle`. |
  | `consumed` | the token was already delivered on a prior poll | stop; if you didn't store it, re-pair. |
  | `expired`  | the 10-min window elapsed | stop; start over at `pair/start`. |
  | `unknown`  | handle not recognized | stop; start over at `pair/start`. |
- **`400`** `{ "error": … }` — missing/invalid `pollHandle`.

⚠️ **The token is delivered exactly once** (on the first `claimed` poll, then the
status becomes `consumed`). The firmware **must** persist it durably on that response.
If it's lost (e.g. a crash before the write), it cannot be re-fetched — the pilot
revokes the device and re-pairs.

Recommended poll cadence: every **3–5 s**, stopping at `expiresAt` (~10 min max).

## 3) `POST /api/ingest`

Upload one IGC flight.

- **Request:**
  - Header `Authorization: Bearer llk_…` (the stored token) — **required**.
  - Header `X-Filename: <name>.igc` — optional; defaults to `device.igc`.
  - Body: the **raw IGC file bytes** (single flight per request; not multipart).
    Content-Type may be `application/octet-stream` or `text/plain`.
- **Response `200`:**
  ```json
  { "flightId": "tn8t", "status": "ready", "deduped": false }
  ```
  | field      | type    | notes |
  |------------|---------|-------|
  | `flightId` | string  | the flight's id in Leaf Log. |
  | `status`   | string  | `"ready"` (parsed OK) or `"failed"` (bad/empty IGC — stored as a failed flight, still counts as delivered). |
  | `deduped`  | boolean | `true` if this exact flight was already uploaded (safe no-op). |
- **Errors:**
  | code  | when | firmware action |
  |-------|------|-----------------|
  | `401` | missing/invalid/revoked token | stop uploading; surface **"reconnect your Leaf"** to the pilot (token was revoked or is wrong). |
  | `400` | empty/invalid body | skip this file. |
  | `413` | body > **5 MB** | skip; flag oversized. |
  | `5xx` | transient server error | retry with backoff. |

**Idempotency:** uploads are de-duplicated by exact file bytes per account, so
**retries are safe** — re-uploading a flight you're unsure delivered just returns
`deduped: true`. Prefer to over-retry than to lose a flight.

---

## Formats

| thing | format |
|-------|--------|
| Pairing code | **6 chars** from the unambiguous alphabet `23456789ABCDEFGHJKMNPQRSTUVWXYZ` (no `0 O 1 I L`). Case-insensitive on entry. |
| Pairing TTL | **10 minutes** from `pair/start`. |
| Device token | opaque string, prefix `llk_`, ~43 url-safe base64 chars after the prefix. Treat as a secret; store it verbatim. |
| Max upload size | **5 MB** per IGC. |

---

## Firmware responsibilities / recommended behavior

- **One token per on-device pilot profile.** Each profile pairs separately and stores
  its own `llk_…`; uploads use that profile's token → the flight lands in that pilot's
  account. The server identifies the account solely from the token (no MAC/email sent).
- **Buffer-then-upload.** Flights recorded before pairing can be uploaded once a token
  exists — hold them on the device and push when connected + paired.
- **Persist the token durably** (survive reboots). It's only re-obtainable by re-pairing.
- **Upload loop:** for each un-uploaded flight, `POST /api/ingest`; on `200` mark it
  delivered (even if `deduped`/`failed`); on `5xx`/network error, retry later with
  backoff; on `401`, stop and prompt the pilot to reconnect.
- **Revocation is normal.** If the pilot revokes the device in the website, uploads
  start returning `401` — the firmware should detect this and prompt a re-pair rather
  than retrying forever.
- **Base URL should be configurable** (to point at a staging/self-hosted instance),
  defaulting to `https://leaflog.norcalflight.com`.

## Not the firmware's job
- The **claim** step (pilot enters the code) happens on the Leaf Log website — there is
  no device-facing claim endpoint.
- Flight **visibility** (private/friends/public) is the pilot's account setting; device
  uploads inherit it. The firmware sends only the IGC.

---

## Test it without a device (curl)

```bash
BASE=https://leaflog.norcalflight.com

# 1. start pairing
curl -s -X POST $BASE/api/devices/pair/start
# -> {"code":"B2DUTQ","pollHandle":"…","expiresAt":"…"}

# 2. (pilot enters B2DUTQ at $BASE/settings/devices while signed in)

# 3. poll until claimed
curl -s -X POST $BASE/api/devices/pair/poll \
  -H 'Content-Type: application/json' \
  -d '{"pollHandle":"…the handle…"}'
# -> {"status":"pending"}  … then  {"status":"claimed","token":"llk_…"}

# 4. upload a flight
curl -s -X POST $BASE/api/ingest \
  -H "Authorization: Bearer llk_…" \
  -H "X-Filename: 2026-07-02-XSX-flight.igc" \
  --data-binary @flight.igc
# -> {"flightId":"tn8t","status":"ready","deduped":false}
```
