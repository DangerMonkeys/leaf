#!/usr/bin/env python3
'Package only changed/new firmware-rendered logbook maintenance screens.'
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SCREENS = [
    ("logging-repair-selected", "Logging settings", "Repair Logs selected"),
    ("checking-logs", "Checking logs", "Manual maintenance"),
    ("repairing-logs", "Repairing logs", "Recovery in progress"),
    ("repair-complete", "Repair complete", "All affected logs recovered"),
    ("no-problems-found", "No problems found", "Healthy logbook"),
    ("logbook-opening", "Loading Flights", "Opening and paging"),
    ("logbook-unavailable", "Flight needs repair", "Confirmed invalid/incomplete log"),
    ("logbook-read-retry", "Could not read flight", "Temporary read failure"),
    ("repair-incomplete", "Some logs not repaired", "Conflict preserved"),
    ("repair-unavailable", "Storage unavailable", "Explicit failure"),
    ("repair-cancelled", "Repair cancelled", "Back / center cancellation"),
    ("repair-busy", "Logs busy", "Another storage transaction"),
    ("repair-flight-active", "Flight timer running", "Maintenance unavailable"),
]

def font(size):
    for name in ("C:/Windows/Fonts/segoeui.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()

def sheet(folder, screens, columns, name, title):
    width, height, margin = 228, 450, 16
    rows = (len(screens) + columns - 1) // columns
    canvas = Image.new("RGB", (columns * width + (columns + 1) * margin,
                               rows * height + (rows + 1) * margin + 52), "#f3f4ef")
    draw = ImageDraw.Draw(canvas)
    draw.text((margin, 13), title, font=font(25), fill="#202a25")
    for i, (key, label, detail) in enumerate(screens):
        x = margin + (i % columns) * (width + margin)
        y = 52 + margin + (i // columns) * (height + margin)
        draw.rounded_rectangle((x, y, x + width, y + height), 8, fill="white")
        draw.text((x + 12, y + 8), label, font=font(18), fill="#202a25")
        draw.text((x + 12, y + 32), detail, font=font(14), fill="#56655b")
        with Image.open(folder / "screenshots" / (key + ".png")) as frame:
            assert frame.size == (96, 192)
            canvas.paste(frame.convert("RGB").resize((192, 384), Image.Resampling.NEAREST),
                         (x + 18, y + 58))
    canvas.save(folder / name)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--screenshots", type=Path)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "docs/ui-reviews/logbook-maintenance")
    args = parser.parse_args()
    output = args.output
    destination = output / "screenshots"
    destination.mkdir(parents=True, exist_ok=True)
    if args.screenshots:
        for key, _, _ in SCREENS:
            shutil.copy2(args.screenshots / (key + ".png"), destination / (key + ".png"))
    sheet(output, SCREENS[:5], 5, "01-maintenance-flow.png", "Logbook maintenance: settings and normal flow")
    sheet(output, SCREENS[5:], 3, "02-maintenance-outcomes.png", "Logbook maintenance: unavailable records and other outcomes")
    changed = [entry for entry in SCREENS if entry[0] in
               ("logbook-opening", "logbook-unavailable", "logbook-read-retry")]
    sheet(output, changed, 3, "03-loading-and-errors.png", "Logbook loading and read outcomes")
    cards = []
    for key, label, detail in SCREENS:
        cards.append(f'<article><h2>{html.escape(label)}</h2><p>{html.escape(detail)}</p>'
                     f'<a href="screenshots/{key}.png"><img src="screenshots/{key}.png" '
                     f'alt="{html.escape(label)}"></a></article>')
    (output / "index.html").write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Logbook maintenance</title>
<style>body{font:16px system-ui;background:#f3f4ef;color:#202a25;margin:24px}
main{display:flex;flex-wrap:wrap;gap:18px}article{background:white;border-radius:12px;padding:18px;width:220px}
h2{font-size:18px;margin:0}p{color:#56655b}img{width:192px;height:384px;image-rendering:pixelated}
a{color:inherit}</style><h1>Logbook maintenance</h1>
<p>Only changed/new screens. Native 96 × 192 captures rendered by the firmware in the simulator;
display enlargement uses nearest-neighbor scaling. Click a screen for its native PNG.</p>
<main>''' + "".join(cards) + "</main></html>", encoding="utf-8")
    sources = [
        "src/vario/logbook/logbook_entry.cpp", "src/vario/comms/leaf_log_sync.cpp",
        "sim/tests/logbook_io.cpp", "sim/tests/leaf_log_sync.cpp",
        "src/vario/logbook/logbook_store.cpp", "src/vario/logbook/logbook_store.h",
        "src/vario/logbook/logbook_io.cpp", "src/vario/taskman.cpp", "src/vario/comms/webserver.cpp",
        "src/vario/ui/display/pages/menu/page_logbook.cpp",
        "src/vario/ui/display/pages/menu/page_repair_logs.cpp",
        "src/vario/ui/display/pages/menu/page_menu_log.cpp", "sim/tests/logbook_maintenance.cpp",
    ]
    sources = list(dict.fromkeys(sources + [str(Path(p).with_suffix(".h")).replace("\\", "/")
                                            for p in sources if (ROOT / Path(p).with_suffix(".h")).exists()]))
    manifest = {
        "source_base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_files_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
        "capture_test": "sim/tests/logbook_maintenance.cpp",
        "native_dimensions": [96, 192],
        "screens": [{"file": "screenshots/" + key + ".png", "title": label,
                     "sha256": hashlib.sha256((destination / (key + ".png")).read_bytes()).hexdigest()}
                    for key, label, _ in SCREENS],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Packaged {len(SCREENS)} changed/new firmware screens: {output}")

if __name__ == "__main__":
    main()
