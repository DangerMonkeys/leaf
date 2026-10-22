"""Losslessly compress simulator PNGs and make a local review gallery/contact sheet."""
from pathlib import Path
import html
import json
import textwrap
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
labels = [
    ("Charging, no popup", "Baseline before sync or with no active notice."),
    ("Checking flights…", "Find eligible flights on the SD card."),
    ("Starting WiFi…", "Selected flight needs a saved WiFi connection."),
    ("Connecting…", "WiFi ready; waiting for a valid network clock."),
    ("Uploading 1 of 2", "First flight upload and response handling."),
    ("Preparing 2 of 2", "Select/recheck the next flight after the first result."),
    ("Uploading 2 of 2", "Second flight upload and response handling."),
    ("Uploaded 2 of 2 / Preparing USB…", "Batch processed; SD handoff is next."),
    ("Uploads complete / USB drive ready", "Five-second completion notice."),
    ("Charging after completion", "Completion notice closed; no popup."),
    ("Upload paused / Retry scheduled", "Transient failure; visible for 30 seconds."),
    ("Retry pending, popup hidden", "Retry still scheduled after notice disappears."),
    ("Hold center to turn on", "Waiting to distinguish tap from hold. Text clips."),
    ("USB unavailable", "USB handoff failed; press retries USB."),
    ("Uploads complete / USB unavailable", "Batch processed but USB handoff failed."),
    ("Turning On / Please wait…", "Power-on request without a confirmed mounted USB host."),
    ("Turning On / Closing USB…", "Power-on request with mounted host; SD return pending."),
    ("No SD card, no popup", "Missing-card indicator is part of the charging page."),
    ("Preparing 100/100", "Compact counter fallback for a large queue."),
    ("Uploading 100/100", "Compact counter fallback for a large queue."),
    ("Uploaded 1000/1000", "Compact counter fallback while preparing USB."),
    ("Preparing", "Verb-only fallback when even a compact counter is too wide."),
    ("Uploading", "Verb-only fallback when even a compact counter is too wide."),
    ("Uploaded", "Verb-only fallback while preparing USB."),
]
files = sorted(root.glob("[0-9][0-9]-*.png"))
assert len(files) == len(labels), (len(files), len(labels))
manifest = []
for path, (title, trigger) in zip(files, labels):
    with Image.open(path) as image:
        image.load()
        assert image.size == (384, 768)
        original = image.tobytes()
        image.save(path, optimize=True)
    with Image.open(path) as image:
        assert image.tobytes() == original
    manifest.append({"file": path.name, "name": title, "shown_when": trigger})
(root / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

cards = "\n".join(
    '<article><h2>{:02d} · {}</h2><a href="{}"><img src="{}" alt="{}" loading="lazy"></a><p>{}</p><a class="file" href="{}">{}</a></article>'.format(
        i, html.escape(m["name"]), m["file"], m["file"], html.escape(m["name"]),
        html.escape(m["shown_when"]), m["file"], m["file"])
    for i, m in enumerate(manifest)
)
document = """<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Leaf charging dialogs · October 21, 2026</title>
<style>
body{margin:0;background:#eef0eb;color:#222;font:16px/1.5 system-ui,sans-serif}
header,main{max-width:1500px;margin:auto;padding:24px}h1{margin:0;font-size:30px}
header p{max-width:1000px}a{color:#23523c}.flow{background:#fff;padding:16px;border-radius:8px}
main{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:18px;padding-top:0}
article{background:white;border:1px solid #ccd1c8;border-radius:9px;padding:16px;display:flex;flex-direction:column;align-items:center}
h2{font-size:16px;min-height:48px;margin:0 0 14px;width:100%}img{width:192px;height:384px;image-rendering:pixelated}
article p{font-size:14px;width:100%;flex:1}.file{font-size:12px;overflow-wrap:anywhere;width:100%}
</style><header><h1>Leaf charging dialog catalog</h1>
<p>October 21, 2026 · 24 simulator screenshots, including ordinary charging-page context and counter-width variants. Click any display for its individual PNG.</p>
<p><a href="README.txt">Message list, triggers, timing, and flow notes</a> · <a href="contact-sheet.png">All screenshots on one sheet</a> · <a href="manifest.json">Screenshot manifest</a></p>
<p class="flow"><strong>Normal flow:</strong> Checking flights → [Starting WiFi → Connecting (network time)] → Uploading 1 → Preparing 2 → Uploading 2 → Uploaded / Preparing USB → Uploads complete / USB ready → Charging.<br>
<strong>Branches:</strong> Failure → retry notice → retry later. Center tap → cancel/open USB. Center hold → Turning On → startup.</p>
<p>The real firmware draws every screen. Offline transport and USB outcomes are controlled by a capture-only harness. Short state boundaries are captured explicitly; normal display timing may skip them. “Uploads complete” can include rejected files. See the notes for the eject-path wording and all caveats.</p>
</header><main>""" + cards + "</main></html>"
(root / "index.html").write_text(document, encoding="utf-8")

font_path = Path("C:/Windows/Fonts/arial.ttf")
font = ImageFont.truetype(str(font_path), 15)
small = ImageFont.truetype(str(font_path), 12)
sheet = Image.new("RGB", (6 * 238, 90 + 4 * 490), "#eef0eb")
draw = ImageDraw.Draw(sheet)
draw.text((20, 15), "Leaf charging dialogs — October 21, 2026", font=ImageFont.truetype(str(font_path), 26), fill="#222222")
draw.text((20, 52), "Real firmware pixels · numbered files in this folder · full triggers and sequences in README.txt", font=font, fill="#333333")
for i, (path, m) in enumerate(zip(files, manifest)):
    x, y = (i % 6) * 238, 90 + (i // 6) * 490
    draw.rounded_rectangle((x + 5, y + 5, x + 233, y + 483), radius=7, fill="white")
    title = f"{i:02d}  {m['name']}"
    for line, text in enumerate(textwrap.wrap(title, 27)):
        draw.text((x + 14, y + 12 + line * 18), text, font=font, fill="#222222")
    with Image.open(path) as image:
        sheet.paste(image.resize((192, 384), Image.Resampling.NEAREST), (x + 23, y + 72))
    draw.text((x + 14, y + 462), path.name[:33], font=small, fill="#555555")
sheet.save(root / "contact-sheet.png", optimize=True)
print(f"Verified {len(files)} PNGs; generated index.html, manifest.json, contact-sheet.png.")
