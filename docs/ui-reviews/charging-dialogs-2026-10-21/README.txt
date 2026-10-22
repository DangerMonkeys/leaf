CHARGING-PAGE DIALOG CATALOG
Captured October 21, 2026. Source commit: 605375e112ede35aa6417ec21fcd8d5aa300adb7.

Open index.html for a labeled screenshot gallery. Individual PNGs are in this folder.
All screenshots show the full 96 x 192 firmware display at an exact 4x pixel scale.
Numbers in names group the normal flow first, then alternative paths; they do not
mean that every screenshot occurs in a single session.

WHAT WAS EXERCISED

The simulator boots the real firmware and uses the real LeafLogSync state machine,
chargingPage_draw(), fonts, u8g2 buffer, and simulator PNG capture. A capture-only
upload transport supplies deterministic outcomes without contacting Leaf Log.
Capture-only variants of the simulator's SD/USB stand-ins supply USB success,
failure, host enumeration, and a delayed return of SD ownership. The normal
simulator always declines USB presentation, so success and Closing USB cannot be
reached with its default stand-ins. The firmware renderer/state machine were not
changed. Fake files and credentials stay inside the temporary container.

The harness drives state boundaries explicitly and redraws there, so brief states
are preserved. This is a catalog of drawable states and their source-defined
ordering, not a promise that every state lasts long enough to see in normal use.
The charging display normally refreshes on a 500 ms timer; eligibility scanning
also runs between display ticks. Hardware/network timing can skip visible frames.

MESSAGE LIST (exact strings; values 1/2 are representative counters)

00-charging-no-popup.png
  Ordinary charging page. Context before Leaf Log starts or when no dialog is active.

01-leaf-log-checking-flights.png
  Title: Leaf Log
  Checking flights...
  Press to cancel
  and open USB drive.
  Hold to turn on.
  Eligibility scan after entering charging with Leaf Log enabled, linked credentials,
  and a firmware-accessible SD card/logbook. Finds pending flights and records local
  rejections. Can also appear briefly for a scan that finds zero pending items.

02-leaf-log-starting-wifi.png
  Title: Leaf Log
  Starting WiFi...
  Same cancel/open-USB/hold instructions as 01.
  A pending flight is selected and a suitable WiFi connection is not already ready.
  Omitted if already connected outside the diagnostic network.

03-leaf-log-connecting-network-time.png
  Title: Leaf Log
  Connecting...
  Same cancel/open-USB/hold instructions as 01.
  WiFi is ready but the device clock predates January 1, 2024. This means waiting
  for network time, not uploading or server verification. NTP is started here;
  an eight-second timeout enters retry backoff. Omitted when time is already valid.

04-leaf-log-uploading-1-of-2.png
  Title: Leaf Log
  Uploading 1 of 2
  Same cancel/open-USB/hold instructions as 01.
  The first queued IGC is being uploaded. No byte percentage or separate verifying
  stage is displayed. HTTP upload/response handling belongs to this state.

05-leaf-log-preparing-2-of-2.png
  Title: Leaf Log
  Preparing 2 of 2
  Same cancel/open-USB/hold instructions as 01.
  Between completed items: selects/reclassifies the next queued logbook entry.
  Repeats for each remaining item and may be too brief for a normal display tick.

06-leaf-log-uploading-2-of-2.png
  Title: Leaf Log
  Uploading 2 of 2
  Same cancel/open-USB/hold instructions as 01.
  Next upload; repeats until the session's initial queue has been processed.

07-leaf-log-uploaded-2-of-2-preparing-usb.png
  Title: Leaf Log
  Uploaded 2 of 2
  Preparing USB...
  Please wait...
  Every queued item has reached a terminal result; the state machine is about to
  hand the SD card to USB. This boundary may be very brief. Its screenActive guard
  expires after 30 seconds from the final item's completion if still in scanning.

08-leaf-log-uploads-complete-usb-ready.png
  Title: Leaf Log
  Uploads complete
  USB drive ready
  Successful completion and USB handoff. This notice lasts five seconds, then closes.
  The same text is currently used after a sync triggered by a computer's explicit
  eject, even though that path intentionally keeps the USB medium ejected.

09-charging-after-completion-popup.png
  Plain charging page after the five-second success notice. USB remains available
  after a normal handoff; after an eject-triggered session the medium stays ejected.

10-leaf-log-upload-paused-retry-scheduled.png
  Title: Leaf Log
  Upload paused
  Retry scheduled
  Press to cancel.
  Transient failure: WiFi, network time, HTTP/transport, scan/queue IO, or persistence
  of an upload/rejection result. Notice lasts 30 seconds. Retries wait five minutes,
  then fifteen minutes, then one hour (one hour thereafter). A successfully
  processed item resets that backoff progression. Press center cancels retry and
  attempts USB handoff; holding center can still request power-on.

11-charging-retry-pending-popup-hidden.png
  Plain charging page after the retry notice's 30-second display window. The retry
  remains scheduled; this is not success or cancellation. The selected flight is
  retried without advancing its counter; a failed initial scan restarts the scan.

12-leaf-log-hold-center-to-turn-on.png
  Title: Leaf Log
  Hold center to turn on
  Press to cancel
  and open USB drive.
  Hold to turn on.
  A center press has requested cancellation but the center pin is still down.
  The sync code waits to distinguish a tap from a hold instead of opening USB
  mid-hold. Release proceeds to USB; holding can finish toward power-on.
  This long status string is not shortened by the progress formatter.

13-leaf-log-usb-unavailable-press-to-retry.png
  Title: Leaf Log
  USB unavailable
  Press to retry.
  Hold to turn on.
  USB presentation failed without a finished batch, e.g. after cancellation or an
  empty/disabled/unlinked session. Notice lasts 30 seconds; state remains unavailable.
  Center retries USB handoff, rather than restarting Leaf Log uploads.

14-leaf-log-uploads-complete-usb-unavailable.png
  Title: Leaf Log
  Uploads complete
  USB unavailable
  Press to retry.
  Hold to turn on.
  The batch finished but USB handoff failed. Notice lasts 30 seconds; press retries
  USB, hold requests power-on. A successful USB retry leads to 08 for five seconds.

15-turning-on-please-wait.png
  Title: Turning On
  [blank status line]
  Please wait...
  A power-on request is pending while USB is not confirmed mounted by a computer,
  e.g. during scanning/upload/retry or on a charge-only cable. Takes priority over
  normal Leaf Log text whenever the dialog is active. No Closing USB claim is made.

16-turning-on-closing-usb.png
  Title: Turning On
  Closing USB...
  Please wait...
  Power-on requested with SD ownership given to USB and a real host mount confirmed.
  Disconnects USB and waits for firmware SD ownership/remount before normal startup.
  The capture holds ownership return temporarily so this state can be inspected.

17-charging-no-sd-card-no-popup.png
  Plain charging page with NO SD! indicator. The missing-card indicator is part of
  the page, not another popup. Included as charging-page context.

18-leaf-log-preparing-compact-counter.png
19-leaf-log-uploading-compact-counter.png
20-leaf-log-uploaded-compact-counter.png
  Same stages and helper text as 05/06/07. The status line shortens to Preparing
  100/100, Uploading 100/100 (100-item queue), or Uploaded 1000/1000 (1,000-item
  queue) because the longer "N of M" form does not fit. Uploaded is one letter
  shorter, so its counter format switches at a different width.

21-leaf-log-preparing-without-counter.png
22-leaf-log-uploading-without-counter.png
23-leaf-log-uploaded-without-counter.png
  Same stages and helper text as 05/06/07, with a 10,000-item queue. Only Preparing,
  Uploading, or Uploaded is shown because even the compact counter does not fit.

ORDER AND BRANCHES TO PRESERVE

Normal two-flight session (optional states in brackets):
  01 -> [02] -> [03] -> 04 -> 05 -> [02] -> [03] -> 06 -> 07 -> 08 -> 09
  Existing WiFi/time skip 02/03. With more flights, repeat Preparing N, optional
  WiFi/time setup, and Uploading N. The total comes from the initial queue snapshot;
  new files discovered later belong to the next session.

Transient failure:
  01/02/03/04/05/06 -> 10 -> (after 30 s) 11 -> retry previous scan/selected item.
  A repeated failure returns to 10 with the next backoff. No verifying popup follows
  an upload and no separate error-reason popup is displayed on the charging page.

Cancellation / tap versus hold:
  Active work or backoff -> center press -> [12 while center remains down].
  Release -> attempt USB -> plain charging if successful, otherwise 13.
  Hold -> 15 or 16 -> ordinary power-on startup once SD ownership is ready.
  An in-progress upload first unwinds with a Cancelled transport outcome.
  There is no dedicated Cancelled or Opening USB dialog in the current renderer.

Successful batch / failed USB:
  07 -> 14 -> center retry -> 08 -> 09, or hold -> 15 -> startup.
  14 hides after 30 s even when no retry is attempted; failure state persists.

Computer eject:
  Host owns USB -> explicit eject -> new eligibility/upload session -> completion.
  This session keeps the medium ejected to avoid remounting it against the host's
  request. Duplicate ejects do not trigger repeated sessions. Completion currently
  reuses 08's USB drive ready wording despite the medium remaining ejected.

No eligible uploads / Leaf Log off / not linked:
  Skip upload cycle -> attempt USB. Normally plain charging, or 13 on USB failure.
  An enabled/linked but empty scan can briefly show 01. No zero-of-zero upload popup.

Rejected / unauthorized:
  Invalid IGC and oversized upload responses persist a rejection and advance the
  item counter just like delivered items. Therefore Uploaded/Uploads complete can
  include rejected entries; these labels do not prove that every flight was accepted.
  Unauthorized marks linking as reconnect-required and exits toward USB; no dedicated
  authorization popup appears here. Details/results are exposed elsewhere.

Turning on:
  Center hold -> 15 (no confirmed mounted USB host) OR 16 (confirmed mounted host)
  -> firmware regains SD -> splash -> configured main page / safety disclaimer.
  The generic splash/disclaimer belong to normal startup, not charging overlays.
  Power-on is processed before factory/commissioning suppression, so setup policy
  must not trap the user on the charging screen.

RENDERER DETAILS / COVERAGE LIMITS

All reachable semantic charging-popup variants are represented above. Repeated
counter/battery/version values are not separate messages. Progress text tries
"Uploading N of M" / "Preparing N of M" / "Uploaded N of M", then "N/M", then
only the verb if the text is wider than 96 pixels. The numeric fallback layouts
are captured in 18-23; the primary screenshots use a representative two-file batch.

The fallback statusLine strings "Uploading to Leaf Log" and "USB drive unavailable"
are overridden by normal page branches. State::Finishing has a switch case but no
assignment into that state in the current implementation. There is no separate
Processing or Verifying charging popup in this source version.

screenActive() controls visibility independently of powerOnPending(). In particular,
an expired Backoff/MassStorageUnavailable notice is not automatically made visible
by setting the power-on flag until the state changes. The actual request is still
handled first on the next sync update. Preserve this distinction when redesigning
or deliberately change it; do not assume the wait popup is guaranteed on every hold.

The normal simulator also stubs factory discovery. Diagnostic commissioning can
defer Leaf Log and USB work, without adding another charging-popup message here.
Hardware behavior, USB timing, real upload service responses, and physical device
flashing were outside this simulator-only capture task.

REPRODUCE (from the repository root, Docker running)

docker run --rm -v "C:/Users/oxoth/projects/leaf:/leaf" -w /leaf gcc:13 make -f docs/ui-reviews/charging-dialogs-2026-10-21/support/capture.mk capture-catalog

The support directory is self-contained capture code; it replaces simulator USB/SD
objects only for this executable. Ordinary simulator and physical firmware builds
keep their existing implementations. Existing .pio/libdeps must be populated.
Then run support/make_gallery.py with Python/Pillow to rebuild the HTML gallery,
manifest, and contact sheet and losslessly compress the PNGs (pixels verified equal).
The contact-sheet generator uses the Windows Arial font at C:/Windows/Fonts/arial.ttf.

VALIDATION
  Repository formatter completed; simulator build and test-leaf-log-sync passed.
  Capture harness asserts the ordinary flow, time/WiFi gates, retry resumption,
  notice expiry, cancellation intent, USB retry, power-on ownership wait,
  unauthorized exit, rejected results, and real large-queue progress states.
  All 24 screenshot dimensions and lossless PNG recompression are checked by the
  gallery generator; contact sheet visually inspected. No hardware build or flash.
  Visible version metadata in these captures: v0.2.4-605+h3.2.8. The simulator was
  compiled with its default leaf_3_2_7 variant; this metadata is the local generated
  version label and does not establish hardware verification.

SOURCE MAP
  src/vario/ui/display/pages/primary/page_charging.cpp: popup layout, strings, buttons.
  src/vario/comms/leaf_log_sync.cpp and .h: ordering, timers, counts, cancellation.
  src/vario/comms/leaf_log_client.cpp: synchronous HTTP upload and response outcomes.
  src/vario/storage/sd_card.cpp: real USB/SD ownership handoff and explicit eject.
  src/vario/taskman.cpp: scan slicing, 500 ms charging refresh, transition to startup.
