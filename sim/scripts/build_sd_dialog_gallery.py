"""Package native firmware captures for visual review; preserve every screenshot pixel."""

import argparse
import hashlib
import html
import json
import math
import textwrap
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


CHARGING = {
    "scanning-repair": ("Leaf Log: scanning during repair", "Captured inside the actual candidate-promotion callback during upload eligibility scanning; no repair-specific alert."),
    "cancel-confirmation": ("Leaf Log: cancel confirmation", "First center tap during upload; existing second-tap confirmation."),
    "cancel-expired": ("Leaf Log: confirmation expired", "Five seconds without a second tap returns to the upload animation."),
    "cancelled-usb-available": ("USB Drive Available: cancelled upload", "Cancellation completes and the card is handed to USB."),
    "opening-usb": ("Opening USB Drive", "Transient cancellation state before the upload worker has closed and USB owns the card."),
    "complete-9-of-9": ("Leaf Log: complete 9/9", "Existing five-second completion notice before USB handoff."),
    "complete-100-of-100": ("Leaf Log: complete 100/100", "Three-digit completion counts."),
    "finished-during-confirmation": ("Leaf Log: finished during confirmation", "Upload completes while the cancel confirmation is visible."),
    "usb-available": ("USB Drive Available", "Nominal mass-storage handoff after completed uploads."),
    "usb-available-after-one-hour": ("USB Drive Available: one hour later", "Persistent existing USB availability state."),
    "usb-ejected": ("USB drive ejected", "Ejected card is retained by firmware; the USB availability dialog disappears."),
    "closing-usb": ("Closing USB Drive", "Power-on waits for USB ownership to be released."),
    "turning-on": ("Turning On", "Power-on from charging mode."),
    "turning-on-during-upload": ("Turning On: upload interrupted", "Existing pending-power-on state while an upload worker closes."),
    "usb-unavailable": ("USB Drive Not Ready", "Mass-storage presentation failed; existing whole-card status."),
    "paused-after-30-seconds": ("Leaf Log: paused after 30 seconds", "Existing retry/paused screen persists rather than silently handing off the card."),
    "version-footer": ("Charging: simulator version footer", "Present SD card; this simulator uses its unchanged build.script.error placeholder, rather than a release version. The placeholder is wider than normal firmware versions."),
    "no-sd-card": ("Charging: NO SD CARD!", "Existing missing-card warning replaces the footer."),
}
PHASES = {
    "scanning": ("Scanning", "Existing scan animation; quiet eligibility repair happens behind this display."),
    "wifi": ("Wi-Fi", "Existing Wi-Fi connection animation."),
    "connecting": ("Connecting", "Existing waiting-for-time/server connection animation."),
    "uploading": ("Uploading", "Existing upload animation and dynamic progress count."),
    "paused": ("Paused", "Existing upload retry animation; no new per-file repair warning."),
}


def charging_entry(file):
    key = file.stem
    if key in CHARGING:
        title, condition = CHARGING[key]
    else:
        phase, frame = key.rsplit("-", 1)
        title, condition = PHASES[phase]
        title = f"Leaf Log: {title} — frame {frame}"
    return {
        "id": f"charging/{key}", "title": title, "condition": condition,
        "renderer": "src/vario/ui/display/pages/primary/page_charging.cpp",
        "file": f"screenshots/charging/{file.name}",
    }


def charging_order(file):
    phase, _, frame = file.stem.rpartition("-")
    if phase in PHASES and frame.isdigit():
        return list(PHASES).index(phase), int(frame)
    return 10, list(CHARGING).index(file.stem)


def font(size, bold=False):
    candidates = [
        Path("C:/Windows/Fonts") / ("segoeuib.ttf" if bold else "segoeui.ttf"),
        Path("/usr/share/fonts/truetype/dejavu") / ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default(size=size)


def contact_sheet(directory, filename, title, entries):
    columns, card_width, card_height, top = 4, 234, 470, 82
    sheet = Image.new("RGB", (columns * card_width + 24, top + math.ceil(len(entries) / columns) * card_height + 16), "#eef0ed")
    draw = ImageDraw.Draw(sheet)
    draw.text((18, 12), title, font=font(23, True), fill="#202823")
    draw.text((18, 47), "Actual firmware pixels at 2× • labels are outside the device screen", font=font(15), fill="#4c5951")
    for index, entry in enumerate(entries):
        x = 12 + (index % columns) * card_width
        y = top + (index // columns) * card_height
        image = Image.open(directory / entry["file"]).convert("RGB").resize((192, 384), Image.Resampling.NEAREST)
        sheet.paste(image, (x + 15, y))
        label = f"{entry['number']:02d}. {entry['title']}"
        for line_index, line in enumerate(textwrap.wrap(label, 29)[:3]):
            draw.text((x + 6, y + 392 + line_index * 21), line, font=font(15, line_index == 0), fill="#202823")
    sheet.save(directory / filename, optimize=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory = args.directory.resolve()
    manifest = json.loads((directory / "manifest-firmware.json").read_text())
    entries = manifest["screens"]
    entries.extend(charging_entry(file) for file in sorted((directory / "screenshots/charging").glob("*.png"), key=charging_order))
    hashes = {}
    for number, entry in enumerate(entries, 1):
        entry["number"] = number
        file = directory / entry["file"]
        with Image.open(file) as original:
            image = original.convert("RGB")
            assert image.size == (96, 192), file
            pixels = image.tobytes()
            image.save(file, optimize=True)  # Lossless compression only.
        with Image.open(file) as compressed:
            assert compressed.convert("RGB").tobytes() == pixels, file
        digest = hashlib.sha256(pixels).hexdigest()
        entry["pixel_sha256"] = digest
        entry["file_sha256"] = hashlib.sha256(file.read_bytes()).hexdigest()
        if digest in hashes:
            entry["same_pixels_as"] = hashes[digest]
        else:
            hashes[digest] = number

    manifest["capture_count"] = len(entries)
    manifest["unique_frames"] = len(hashes)
    manifest["notes"] = [
        "Actual 96×192 u8g2 firmware frames; no mockups or visual changes. Synthetic data only; user SD evidence was not used.",
        f"{manifest['source_commit']} is the firmware source commit. Capture tooling and this gallery are a separate follow-up commit.",
        "Saving and shutdown-failure messages were captured inside the real stop path, before staging open and during the failure-notice delay respectively.",
        "Charging uses the existing charging_dialogs regression harness and deterministic transport outcomes; it checks the reference animation artwork.",
        "The charging footer retains the simulator's 0.0.0+build.script.error placeholder. It is not an SD error or a release build version; no footer pixels were edited.",
        "Removed Not fully deleted messages are absent in the current firmware. Quiet partial-delete captures demonstrate the replacement behavior.",
        "No repair-success, repair-deferred, or cleanup-failure dialogs exist. Repair outcomes appear through the available logbook entries and counts.",
        "Manual and automatic stops share the same summary renderer. Power-off and low-battery failure notices use the same message.",
        "Web-app Checking logs... is HTML, not a firmware LCD screen; it is outside this native screenshot set. Unrelated GPX/profile/formatting tools are also outside this set.",
    ]
    repository = Path(__file__).resolve().parents[2]
    manifest["capture_tools"] = {
        str(file): hashlib.sha256((repository / file).read_bytes()).hexdigest()
        for file in ("sim/tests/sd_dialogs.cpp", "sim/tests/charging_dialogs.cpp", "sim/scripts/build_sd_dialog_gallery.py")
    }
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    groups = [
        ("01-save-and-summary", "Saving and flight summaries", [e for e in entries if e["id"].startswith(("saving/", "summary/"))]),
        ("02-logbook", "Logbook: normal, deletion and data states", [e for e in entries if e["id"].startswith("logbook/") and not any(word in e["id"] for word in ("repair", "backup", "conflict"))]),
        ("03-repair-and-context", "Repair outcomes and menu context", [e for e in entries if e["id"].startswith("context/") or (e["id"].startswith("logbook/") and any(word in e["id"] for word in ("repair", "backup", "conflict")))]),
        ("04-charging-states", "Charging, upload and USB states", [e for e in entries if e["id"].startswith("charging/") and e["id"].split("/")[1] in CHARGING]),
        ("05-charging-animation", "Charging and upload animation frames", [e for e in entries if e["id"].startswith("charging/") and e["id"].split("/")[1] not in CHARGING]),
    ]
    for key, title, items in groups:
        contact_sheet(directory, key + ".png", title, items)

    esc = html.escape
    cards = []
    for key, title, items in groups:
        group_cards = []
        for entry in items:
            duplicate = f"Same pixels as #{entry['same_pixels_as']:02d}. " if "same_pixels_as" in entry else ""
            group_cards.append(f'''<article data-search="{esc(entry['title'] + ' ' + entry['condition'] + ' ' + entry['id'], quote=True)}">
<h3>#{entry['number']:02d} {esc(entry['title'])}</h3>
<a href="{esc(entry['file'])}" target="_blank"><img src="{esc(entry['file'])}" width="288" height="576" loading="lazy" alt="{esc(entry['title'], quote=True)}"></a>
<p>{esc(entry['condition'])}</p><p class="duplicate">{esc(duplicate)}</p>
<details><summary>Capture details</summary><code>{esc(entry['id'])}</code><p>{esc(entry['renderer'])}</p></details></article>''')
        cards.append(f'<section id="{key}"><h2>{esc(title)} <a href="{key}.png">contact sheet</a></h2><div class="grid">{"".join(group_cards)}</div></section>')
    notes = "".join(f"<li>{esc(note)}</li>" for note in manifest["notes"])
    links = " · ".join(f'<a href="#{key}">{esc(title)}</a>' for key, title, _ in groups)
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Leaf SD screens — visual review</title><style>
*{box-sizing:border-box}body{margin:0;background:#eef0ed;color:#202823;font:16px/1.45 system-ui,sans-serif}
header{background:#d8ff00;padding:26px max(24px,calc((100vw - 1400px)/2))}h1{margin:0 0 8px;font-size:30px}header p{margin:4px 0}
main{max-width:1448px;margin:auto;padding:24px}nav{margin:16px 0}a{color:#205c40}header a{color:#172d20}
.toolbar{position:sticky;top:0;background:#eef0edf5;border-bottom:1px solid #c6cec7;padding:12px 0;z-index:2;display:flex;flex-wrap:wrap;gap:16px;align-items:center}
input{font:inherit;padding:9px;border:1px solid #85978b;border-radius:5px;min-width:280px}h2{margin:32px 0 14px}h2 a{font-size:15px;font-weight:400;margin-left:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,330px),1fr));gap:20px}article{background:white;border:1px solid #d0d7d0;border-radius:8px;padding:16px;display:flex;align-items:center;flex-direction:column}
h3{font-size:16px;align-self:stretch;min-height:48px;margin:0 0 12px}img{box-sizing:content-box;display:block;image-rendering:pixelated;border:1px solid #889185;max-width:100%;height:auto}
article p{align-self:stretch;margin:12px 0 0;font-size:14px}.duplicate{color:#576c5f;min-height:20px}details{align-self:stretch;margin-top:10px;font-size:12px;overflow-wrap:anywhere}.notes{padding:8px 22px;background:white;border:1px solid #d0d7d0;border-radius:6px}.notes li{margin:8px 0}
pre{overflow:auto;max-width:100%}body.native img{width:96px;height:192px}body.native article{min-height:420px}[hidden]{display:none!important}
</style><header><h1>Leaf SD screens</h1><p>CAPTURE_COUNT native screenshots · UNIQUE_COUNT distinct frames · firmware source <strong>c88afbb4</strong></p>
<p>Current visuals for review. The firmware’s styling has not been changed.</p></header><main>
<nav>NAVIGATION</nav><details class="notes"><summary>Scope, provenance and screens that intentionally do not exist</summary><ul>NOTES</ul></details>
<div class="toolbar"><input id="filter" type="search" placeholder="Filter titles and SD states" aria-label="Filter screenshots"><label><input id="native" type="checkbox" style="min-width:0"> Native size</label><span id="count"></span></div>
CARDS
<p>PNG files retain the original 96×192 pixels. Open an image for the individual capture. <a href="manifest.json">Full capture manifest and pixel hashes</a>.</p>
<details class="notes"><summary>Regenerate this review set</summary><p>In the normal GCC simulator container, from the repository root (with dependencies available):</p>
<pre>make -C sim -j8 /leaf/sim/build/sd_dialogs_capture /leaf/sim/build/charging_dialogs_test
LEAF_CAPTURE_COMMIT=&lt;firmware-commit&gt; ./sim/build/sd_dialogs_capture docs/ui-reviews/sd-card-dialogs
./sim/build/charging_dialogs_test docs/ui-reviews/sd-card-dialogs/screenshots/charging</pre>
<p>Then, using Python with Pillow installed:</p><pre>python sim/scripts/build_sd_dialog_gallery.py docs/ui-reviews/sd-card-dialogs</pre></details>
<script>const cards=[...document.querySelectorAll('article')];document.getElementById('filter').oninput=e=>{const q=e.target.value.toLowerCase();let n=0;cards.forEach(c=>{c.hidden=!c.dataset.search.toLowerCase().includes(q);if(!c.hidden)n++});document.getElementById('count').textContent=n+' screens'};document.getElementById('native').onchange=e=>document.body.classList.toggle('native',e.target.checked);document.getElementById('count').textContent=cards.length+' screens';</script></main></html>'''
    page = page.replace("<strong>c88afbb4</strong>", f"<strong>{esc(manifest['source_commit'])}</strong>")
    for key, value in {"CAPTURE_COUNT": str(len(entries)), "UNIQUE_COUNT": str(len(hashes)), "NAVIGATION": links, "NOTES": notes, "CARDS": "\n".join(cards)}.items():
        page = page.replace(key, value)
    (directory / "index.html").write_text(page, encoding="utf-8")
    print(f"Packaged {len(entries)} native captures ({len(hashes)} unique frames), five contact sheets and index.html.")


if __name__ == "__main__":
    main()
