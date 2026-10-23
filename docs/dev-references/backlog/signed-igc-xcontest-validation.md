---
title: Signed IGC Files and XContest Validation
description: Staged plan for producing tamper-evident Leaf IGC files that can be validated and scored by XContest.
---

# Signed IGC Files and XContest Validation

Status reconciled **2026-10-22**: authenticated signing and contest acceptance remain proposed.
Checksum generation and a local validator were implemented in `daae85f2` (#414), with a prebuilt
Windows validator added in `82a74de7`. These complete a local integrity-checking step, not this
backlog's signing/acceptance goal. External registration, reviewed signing/key architecture,
production validator deployment and end-to-end contest qualification remain open.

## Goal

Leaf should produce IGC files with a cryptographic G record that:

- detects modification of protected flight data after it was recorded;
- can be checked by a Leaf-specific validation program;
- uses a manufacturer identifier assigned to Leaf rather than one belonging to another product;
- is accepted as valid for scoring by XContest; and
- follows the applicable FAI/CIVL IGC and flight-recorder requirements closely enough for the
  intended approval and competition uses.

The target outcome is not merely an IGC file containing a hash-looking G record. The manufacturer
identifier, firmware signing algorithm, protected-record rules, validation executable, CIVL
registration, and XContest deployment must all agree.

## Current implementation and remaining problem

Leaf pins IgcLogger `v0.0.6` and appends its incremental, unkeyed MD5 G record on normal flight
completion. `utils/igc_validator/` contains a maintained C checker and a standalone Windows
executable. The checker requires an `AXLV` header, at least one B record and a final 32-hex-digit
G record, accepts LF/CRLF and rejects a checksum mismatch. See the
[validator README](../../../utils/igc_validator/README.md) for exact rules and build/use commands.

Current firmware uses `XLV` in A/L records and filenames, replacing the historical `XLF` code.
This repository review establishes the implementation, not an assigned CIVL identifier or deployed
contest validator. The A-record identity is still fixed (`Lea` plus `f1`), and filenames still
use serial `000`; stable recorder identity remains part of the future signing design.

An unkeyed checksum can be recomputed after editing and does not authenticate a recorder.
Neither a passing local checker nor a changed manufacturer code establishes CIVL/XContest
acceptance. Do not present the checksum work as authenticated signing or retroactively sign
historical files. The stages below describe the remaining authenticated-signing program;
their signer/validator scope goes beyond the existing checksum implementation.

Adding firmware signing without resolving the identifier and validator therefore does not make Leaf
files scoreable. Likewise, registering an identifier without implementing matching signing does not
solve the problem.

Relevant current code:

- `src/vario/logbook/igc.h` defines the manufacturer identifier.
- `src/vario/logbook/igc.cpp` creates the A record, headers, fixes, declarations, events, and final
  G record call.
- `platformio.ini` pins the IGC logging library version used by firmware and the simulator.
- `factory_interface/` owns production flashing and commissioning and may need to provision signing
  material, depending on the chosen security design.

## Scope boundaries

This backlog includes the work needed to make newly recorded Leaf flights verifiable. It does not
promise that:

- previously recorded unsigned files can be made valid retroactively;
- a CIVL listing guarantees acceptance by every national contest or competition organizer;
- corrupted or abruptly interrupted flights can always be recovered as signed files; or
- development devices with user-replaceable firmware will necessarily produce contest-valid files.

Any recovery design must not sign data read back from user-editable storage unless CIVL explicitly
accepts that workflow. The normal signing path should protect records as the firmware produces them.

## Stage 1: confirm the external acceptance path

Do this before finalizing the security format, because CIVL and XContest must be able and willing to
deploy the resulting validator.

1. Assign a Leaf project maintainer to own the manufacturer/developer relationship. Requests must
   come from the project or manufacturer, not an individual pilot.
2. Contact FAI/CIVL through the current flight-recorder and validator submission process.
3. Describe Leaf accurately as an ESP32-S3 hardware variometer/flight recorder with internal GNSS,
   pressure sensing, incremental SD-card logging, open-source firmware, and field updates.
4. Confirm an assigned three-character identifier for Leaf. Coordinate the current `XLV` code
   or any required replacement with CIVL; a code in firmware is not proof of registration.
5. Confirm the intended approval/acceptance level and whether CIVL expects an `X..`, `V..`, or other
   class of identifier for Leaf.
6. Agree on the signature algorithm and exact byte canonicalization before shipping it broadly.
7. Confirm the required validator format, status output, packaging, test files, conformity
   declaration, and support lifetime.
8. Contact XContest to confirm how and when it obtains validator updates and how Leaf can perform an
   end-to-end pre-release test.

Deliverables:

- written confirmation of the assigned identifier;
- an agreed signature/validation contract;
- a checklist of CIVL submission artifacts;
- a confirmed XContest deployment/test contact; and
- a documented approval target so firmware claims do not exceed what was actually granted.

Useful starting points:

- [FAI XC instrument acceptance process](https://www.fai.org/page/civl-xc-instrument-accepted)
- [FAI/CIVL Sporting Code Section 7](https://www.fai.org/page/sporting-code-section-7)
- [CIVL supported validation software](http://vali.fai-civl.org/supported.html)
- [CIVL validator FAQ](http://vali.fai-civl.org/faq.html)

## Stage 2: choose the signing and key-protection architecture

Document the decision in this backlog before implementation. At minimum, decide the following.

### Symmetric HMAC or asymmetric signature

HMAC-SHA256 with a 256-bit secret is relatively small, fast, and directly contemplated by CIVL. It
also means a validator must contain or otherwise have access to a secret capable of producing valid
signatures. A model-wide secret increases the impact of one extraction, while per-device secrets
increase provisioning and validator complexity.

An asymmetric design allows validators to contain only public verification material and can use the
ESP32-S3 RSA Digital Signature hardware to protect private-key operations. It provides better key
separation but adds provisioning, G-record size, validator, and interoperability complexity. Obtain
CIVL agreement before choosing a format that differs from common deployed validators.

### Key storage and device trust level

Candidate approaches include:

1. Build-injected and obfuscated shared HMAC key.
   - Lowest implementation and manufacturing cost.
   - Compatible with ordinary firmware builds and field flashing.
   - The secret is still present in public firmware binaries and can eventually be extracted.
   - Appropriate only if CIVL and Leaf explicitly accept this as casual-tamper resistance rather
     than strong device security.

2. Read-protected ESP32-S3 eFuse HMAC key.
   - Removes the raw key from public application firmware and prevents normal software readback.
   - Requires secure factory provisioning, locked key-purpose/read settings, and a plan for failed
     provisioning.
   - Hardware HMAC APIs are not automatically a drop-in replacement for a streaming file HMAC; the
     message construction must be designed and approved.
   - Secure Boot or an equivalent trust boundary is needed to prevent replacement firmware from
     using the device as a signing oracle.

3. ESP32-S3 hardware-backed asymmetric signing.
   - Keeps private signing operations out of ordinary application code and validators.
   - Requires a more involved per-device or per-model key hierarchy and validator design.
   - May require multiple G-record lines and more CPU/flash/provisioning work.

Production devices and developer-unlocked devices may need different policies. One possible product
boundary is that locked production devices emit contest-valid signatures, while developer builds
emit an explicit unsigned/development status and cannot impersonate production files.

### Crypto implementation

Prefer a maintained cryptographic implementation over new handwritten crypto in production
firmware. Leaf already links ESP-IDF/mbedTLS cryptographic code, so an mbedTLS streaming HMAC backend
may have little incremental flash cost. A portable fallback may still be needed for the host
simulator and non-ESP library users, but it should run the same known-answer tests.

### Failure policy

Choose a fail-closed behavior. A missing, placeholder, weak, or unavailable production key must not
silently produce a G record that appears trustworthy. Options are:

- refuse to start a signing-required production track;
- record an explicitly unsigned file with no G record and surface a diagnostic; or
- support a clearly distinct development identifier that cannot be accepted by the production
  validator.

The firmware, UI, logs, and factory checks should make the selected behavior observable.

### Rotation and versioning

Define a signed algorithm/key identifier that allows the validator to select an accepted key or
public key without guessing. Keep old verification material for supported firmware generations, and
document when older generations reach end of support. Never reuse the public placeholder or a test
key in production.

## Stage 3: implement and test the signing component

Incremental checksum generation exists. The authenticated signer, key policy and signing-failure
contract below remain open; do not recreate the checksum as though no G-record path existed.

The logging component should provide an incremental signing interface suitable for Leaf's
record-at-a-time SD writes.

Required behavior:

- Begin a fresh signing context before the A record of each flight.
- Protect exactly the record classes and sources agreed with CIVL.
- Protect Leaf-owned L records while allowing permitted third-party L records to be appended later
  without invalidating the file.
- Define one canonical representation for line endings and apply it identically in firmware and the
  validator.
- Append correctly formatted G record line or lines only after all protected flight records.
- Prevent key replacement after signing starts.
- Prevent duplicate finalization and records written after finalization.
- Return and propagate output, signing, and finalization failures.
- Zeroize sensitive working state where practical.
- Enforce the selected production key length and key-source policy.
- Avoid large buffers or whole-file reads; normal signing must remain incremental.

Include tests in the maintained source tree rather than relying only on an external one-off harness:

- published SHA/HMAC or signature known-answer vectors;
- one-byte and chunked updates crossing internal block boundaries;
- a complete canonical Leaf IGC fixture;
- CRLF and LF representations;
- protected-record tampering;
- missing, malformed, duplicate, and truncated G records;
- Leaf-owned and third-party L records;
- wrong manufacturer identifier;
- wrong key, rotated key, and unsupported key identifier;
- repeated flights using one logger object; and
- signing/output failures and interrupted finalization.

## Stage 4: build the Leaf validation program

The portable checksum checker and Windows distribution already exist. Extend or replace them
for the agreed authenticated format; the requirements below are not claims about today's checker.

The validator is a first-class deliverable, not an afterthought or private test script.

It should:

- verify that the A record contains the assigned Leaf identifier;
- parse IGC records defensively with bounded memory use;
- reproduce the exact protected-record and line-canonicalization rules;
- validate all supported production algorithm/key generations;
- reject missing, malformed, unknown, and mismatched signatures;
- allow only the post-processing records that the agreed rules permit;
- distinguish invalid signatures from unsupported/corrupt input;
- emit the exact machine-readable CIVL status expected by unattended services;
- be packaged as the required static, non-interactive command-line executable without popup UI or
  unavailable runtime dependencies; and
- be reproducible from maintained source without publishing production secrets.

Keep shared canonicalization fixtures that run against both the firmware signer and validator. A
passing test against an independently written reference calculation is necessary; signer and
validator merely agreeing with each other is not enough to prove the cryptography is correct.

## Stage 5: integrate signing into Leaf firmware

1. Update the pinned logging dependency or internal component to a reviewed immutable release.
2. Reconcile `IGC_MANUFACTURER_CODE` (currently `XLV`) with the identifier assigned to Leaf. This must update A records,
   Leaf L records, and long filenames consistently.
3. Replace the current fixed logger identity and filename serial `000` with a stable, documented
   device serial representation that satisfies the assigned format. Consider collision resistance
   and the privacy implications of exposing the full MAC address.
4. Set all required IGC headers and confirm their current formats with the validator, including date,
   flight number if used, pilot, second crew, glider type/ID, firmware/hardware identity, sensor
   identity, GPS datum, GNSS altitude datum, and pressure altitude datum.
5. Acquire the production signing capability before `writeHeader()` and verify that it matches the
   running production policy.
6. Stream every generated protected record through the signing context before or atomically with its
   SD write. A later SD readback must not be the normal source of signed data.
7. Finalize the G record before closing the file on manual stop, auto-stop, controlled power-off,
   firmware-update restart, and every other controlled flight-ending path.
8. Define behavior for watchdog reset, crash, battery removal, SD removal, full card, and write
   failure. An incomplete file should remain recognizably unsigned/invalid rather than falsely valid.
9. Expose signing state and failure reason through diagnostics and, if useful, the logbook/web UI.
10. Ensure release builds that can produce accepted signatures cannot enable sensor-injection or
    other test features that permit fabricated flight data. Keep simulator and development behavior
    explicit and separate.

    Separate dev builds were retired in #420. Current release firmware can opt into UDP injection
    only with both Developer mode and the injection setting enabled. Those runtime gates do not
    themselves establish the production signing trust boundary required here.

Consider periodically preserving enough non-secret signing state to survive expected controlled
transitions, but do not design next-boot signing of user-editable SD data without CIVL approval.

## Stage 6: update manufacturing, release, and secret handling

The selected architecture determines how much factory work is required.

For build-injected secrets:

- keep production values out of Git, source archives, build output, command lines, logs, cache keys,
  simulator artifacts, and publicly downloadable unprotected symbols;
- restrict release builds to an auditable secret-injection environment;
- scan release artifacts and logs for accidental disclosure; and
- document emergency rotation after suspected exposure.

For eFuse or hardware-backed keys:

- add an idempotent, auditable commissioning step to `factory_interface`;
- generate or import keys through a controlled manufacturing path;
- verify key purpose, read protection, write protection, and device identity after provisioning;
- prevent a partially provisioned unit from passing commissioning;
- define whether rework/replacement is possible after an eFuse error;
- decide how DIY and developer units are provisioned or deliberately left unsigned; and
- account for Secure Boot, signed firmware updates, UART/USB download restrictions, recovery, and
  support implications before irreversibly locking devices.

Add a factory self-test that records a small deterministic signed IGC sample and verifies it with the
production validator without exposing the signing secret.

## Stage 7: conformance and end-to-end rollout

Use production-equivalent hardware and firmware for acceptance testing.

1. Generate representative solo, routed-task, saved-point, long, and multi-flight-per-day files.
2. Validate them locally with the exact submitted validator binary.
3. Confirm that byte tampering is rejected.
4. Confirm permitted line-ending conversion and third-party record additions behave as designed.
5. Exercise manual stop, auto-stop, controlled shutdown, low battery, SD-full, SD removal, restart,
   and interrupted-flight cases.
6. Submit the conformity declaration, validator, sample files, technical description, and any other
   requested material to CIVL/FAI.
7. After the assigned identifier and validator are active, test an untouched production-device file
   with the CIVL validation service.
8. Confirm the same file is accepted and scored by XContest in the intended contest context.
9. Publish the supported firmware versions, device classes, identifier, validation expectations, and
   known limitations for pilots and contest administrators.

Do not change stable firmware to the assigned production identifier until the corresponding
validator deployment is coordinated. Otherwise new files may temporarily become unsupported rather
than merely failing the old, incorrect mapping.

## Acceptance criteria

This backlog is complete only when all of the following are true:

- FAI/CIVL has assigned Leaf a manufacturer identifier that is not shared with an unrelated product.
- Leaf has a documented conformity/acceptance scope.
- Production firmware uses the assigned identifier consistently in A records, Leaf L records, and
  filenames.
- Each normally completed production flight ends with a real, verifiable G record.
- Missing or unavailable production signing material fails closed and is observable.
- The selected key/private-key protection and provisioning design has been reviewed and documented.
- Production secrets are absent from the public repository, public build logs, and development/test
  artifacts.
- The maintained validation program supports every production signing generation still in service.
- Signer and validator share comprehensive automated canonicalization and tamper fixtures.
- An untouched production Leaf IGC passes the deployed CIVL validation path and is accepted for
  scoring by XContest.
- Modifying a protected record causes validation failure.
- Permitted CRLF/LF conversion and permitted third-party record additions do not cause false failure.
- Controlled flight-ending and power-off paths finalize the file before it is closed.
- Interrupted or storage-failed files cannot be mistaken for valid signed flights.
- Simulator checks, firmware formatting/build, hardware flight logging, and validator tests pass.
- Pilot, developer, factory, key-rotation, and incident-response documentation is published.

## Decisions still open

1. What exact CIVL acceptance or approval level is Leaf pursuing initially?
2. Which manufacturer identifier will CIVL assign?
3. Does XContest consume the CIVL validator automatically, or require a separate deployment?
4. HMAC-SHA256 or an asymmetric signature?
5. Model-wide, manufacturing-batch, or per-device signing identity?
6. Build-obfuscated secret, eFuse-backed HMAC, or hardware-backed asymmetric private key?
7. Are production units locked with Secure Boot and restricted download mode?
8. Can developer-unlocked devices produce contest-valid files, or must they use a development
   identifier/key that the production validator rejects?
9. What signed field identifies the algorithm/key generation for rotation?
10. How long will validators retain support for older firmware/key generations?
11. What stable device serial should appear in the A record and filename, and how much hardware
    identity should be public?
12. Should the device UI distinguish `signed`, `unsigned development`, and `signing failed` flights?
13. How should interrupted flights be presented or quarantined locally?
14. Does the current IGC date/header form need migration for the assigned validator?

## Principal risks and tradeoffs

- A strong G-record implementation still fails everywhere if the identifier or deployed validator
  is wrong.
- A shared HMAC secret is operationally simple but compromise affects every device using it.
- Per-device keys limit compromise but require device identification and scalable validator key
  management.
- Obfuscation preserves today's open flashing workflow but offers only limited resistance to a
  determined firmware analyst.
- eFuse-backed keys materially improve extraction resistance but can make provisioning irreversible
  and require a trustworthy boot/update chain.
- Secure Boot improves signing integrity but changes factory flashing, developer access, recovery,
  and field-update operations.
- Asymmetric signing removes signing secrets from validators but substantially increases initial
  design and interoperability work.
- Adding signatures only at normal flight end is simple, but abrupt power loss leaves an unsigned
  file. More frequent recoverable finalization increases SD writes and state-machine complexity.
- Changing the manufacturer identifier before validator deployment creates a transition period in
  which otherwise good files are reported as unsupported.
- Contest acceptance remains subject to XContest, national contest, and organizer policies even
  after technical validation succeeds.

## Suggested issue breakdown

After Stage 1 resolves the external requirements, split implementation into independently reviewable
issues:

1. CIVL identifier, conformity, and XContest coordination.
2. Signing/key architecture decision record and threat model.
3. Incremental signer, canonicalization, and automated test vectors.
4. Leaf validator implementation and packaging.
5. Leaf firmware IGC identity/header/finalization integration.
6. Factory provisioning and secure production build pipeline.
7. Hardware fault-path and power-loss testing.
8. CIVL submission, XContest rollout, and public documentation.
