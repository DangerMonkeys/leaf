"""Build fixed-grid 96-pixel previews and a device-pixel-aligned local review page."""
from pathlib import Path
import html
import json
from PIL import Image

ROOT = Path(__file__).resolve().parent
INK = (32, 36, 35)
LCD = (199, 203, 191)


def build_page():
    specs = json.loads((ROOT / 'manifest.json').read_text())
    previews = []
    cards = []
    for spec in specs:
        mask_path = ROOT / spec['asset'].replace('.png', '-mask.png')
        mask = Image.open(mask_path).convert('1')
        x, y = (96 - mask.width) // 2, (32 - mask.height) // 2
        frame = Image.new('1', (96, 32), 0)
        frame.paste(mask, (x, y))
        rows = [''.join('1' if frame.getpixel((xx, yy)) else '0' for xx in range(96)) for yy in range(32)]
        previews.append(dict(id=spec['id'], width=spec['width'], height=spec['height'], x=x, y=y, rows=rows))
        colored = Image.new('RGB', (96, 32), LCD)
        colored.paste(INK, (0, 0), frame)
        frame_name = spec['asset'].replace('.png', '-frame96x32.png')
        colored.save(ROOT / frame_name)
        assert frame.getbbox() == (x, y, x + mask.width, y + mask.height)
        cards.append(f'''<article>
<h2>{spec['id']} <span>{html.escape(spec['title'])}</span></h2>
<p class="dimensions">{spec['width']} × {spec['height']} wordmark · 96 × 32 preview</p>
<div class="large-scroll"><div class="pixel-slot" data-id="{spec['id']}" data-view="large"><canvas role="img" aria-label="{spec['id']} enlarged on a 96-pixel display"></canvas></div></div>
<div class="native-row"><span>1× positive</span><div class="pixel-slot" data-id="{spec['id']}" data-view="native"><canvas role="img" aria-label="{spec['id']} native positive"></canvas></div><span>1× inverse</span><div class="pixel-slot" data-id="{spec['id']}" data-view="inverse"><canvas role="img" aria-label="{spec['id']} native inverse"></canvas></div></div>
<p class="note">{html.escape(spec['note'])}</p>
<a href="{frame_name}">96 × 32 PNG</a> · <a href="{spec['asset']}">Original {spec['width']} × {spec['height']} PNG</a>
</article>''')
    page = '''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LOG — pixel-accurate display comparison</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#eef0eb;color:#202423;font:15px/1.5 system-ui,sans-serif}
header{padding:24px 24px 16px;max-width:1150px}h1{font-size:28px;margin:0 0 8px}header p{margin:8px 0;max-width:960px}
.toolbar{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin:18px 0 10px}button{font:inherit;padding:7px 13px;background:white;color:#202423;border:1px solid #a5b19c;border-radius:5px;cursor:pointer}button[aria-pressed=true]{background:#202423;color:white;border-color:#202423}
.status{font-size:13px;color:#48603e;font-variant-numeric:tabular-nums}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,430px),1fr));gap:16px;padding:8px 24px 24px;max-width:1400px}
article{background:white;border:1px solid #d1d7ca;border-radius:8px;padding:18px;min-width:0}h2{margin:0;font-size:20px}h2 span{font-weight:500;margin-left:10px;font-size:17px}.dimensions{font-size:13px;margin:4px 0 18px;color:#596452}
.large-scroll{overflow-x:auto;overflow-y:hidden;padding-bottom:4px}.pixel-slot{position:relative;flex:none;contain:layout paint;display:block}canvas{display:block;position:absolute;image-rendering:pixelated;max-width:none;max-height:none;border:0;padding:0;margin:0;box-sizing:content-box}
.native-row{display:flex;align-items:center;gap:10px;margin:20px 0 8px;flex-wrap:wrap;font-size:12px}.note{font-size:13px;min-height:40px}a{color:#23523c;font-size:13px}noscript{display:block;margin:24px}
</style>
<header><h1>LOG · pixel-accurate display comparison</h1>
<p>All eight wordmarks sit on a fixed <strong>96 × 32 Leaf-pixel strip</strong>. Letter placement is baked into the bitmap at whole-pixel coordinates.</p>
<p><strong>1× = one physical screen pixel per Leaf pixel.</strong> Enlarged views use exact integer blocks. The page adjusts for browser zoom and display density, and aligns each bitmap to the physical pixel grid.</p>
<div class="toolbar" aria-label="Enlargement"><span>Enlarged view:</span><button data-scale="2" aria-pressed="false">2×</button><button data-scale="4" aria-pressed="true">4×</button><button data-scale="8" aria-pressed="false">8×</button><span style="margin-left:12px">Appearance:</span><button data-theme="light" aria-pressed="true">Positive</button><button data-theme="dark" aria-pressed="false">Inverse</button></div>
<p id="status" class="status" role="status"></p>
<p><a href="index.html">Original studies and reference</a> · <a href="pixel-patterns.txt">Pixel patterns</a></p>
</header><main class="grid">''' + '\n'.join(cards) + '''</main>
<noscript>Enable JavaScript for display-density-aware previews. Each card also links to the fixed 96 × 32 PNG.</noscript>
<script>
const previews = ''' + json.dumps(previews, separators=(',', ':')) + ''';
const byId = new Map(previews.map(item => [item.id, item]));
let scale = 4, theme = 'light', queued = false, densityQuery;
const ink = '#202423', lcd = '#c7cbbf';

function alignSlot(slot, density) {
  const canvas = slot.querySelector('canvas');
  const box = slot.getBoundingClientRect();
  // The wrapper is allowed to have a fractional position. Move the canvas origin
  // to the next physical pixel without scaling or transforming the pixel artwork.
  canvas.style.left = ((Math.ceil(box.left * density - 1e-7) / density) - box.left) + 'px';
  canvas.style.top = ((Math.ceil(box.top * density - 1e-7) / density) - box.top) + 'px';
}

function paintSlot(slot, density) {
  const item = byId.get(slot.dataset.id), view = slot.dataset.view;
  const factor = view === 'large' ? scale : 1;
  const inverse = view === 'inverse' || (view === 'large' && theme === 'dark');
  const canvas = slot.querySelector('canvas');
  canvas.width = 96 * factor;
  canvas.height = 32 * factor;
  // Backing pixels map 1:1 onto physical display pixels, including at fractional DPR.
  canvas.style.width = (canvas.width / density) + 'px';
  canvas.style.height = (canvas.height / density) + 'px';
  slot.style.width = (canvas.width / density + 2) + 'px';
  slot.style.height = (canvas.height / density + 2) + 'px';
  const ctx = canvas.getContext('2d', {alpha:false});
  ctx.imageSmoothingEnabled = false;
  ctx.fillStyle = inverse ? ink : lcd;
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = inverse ? lcd : ink;
  for (let y=0; y<32; y++) {
    const row = item.rows[y];
    for (let x=0; x<96; x++) {
      if (row[x] === '1') ctx.fillRect(x*factor, y*factor, factor, factor);
    }
  }
}

function alignAll() {
  const density = window.devicePixelRatio || 1;
  document.querySelectorAll('.pixel-slot').forEach(slot => alignSlot(slot, density));
}

function render() {
  const density = window.devicePixelRatio || 1;
  document.querySelectorAll('.pixel-slot').forEach(slot => paintSlot(slot, density));
  alignAll();
  document.getElementById('status').textContent = `Enlarged strips: ${96*scale} × ${32*scale} physical pixels · ${scale} × ${scale} screen pixels per Leaf pixel · display density ${density.toFixed(2)}`;
  document.querySelectorAll('[data-scale]').forEach(button => button.setAttribute('aria-pressed', String(Number(button.dataset.scale) === scale)));
  document.querySelectorAll('[data-theme]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.theme === theme)));
}

function queueAlign() {
  if (queued) return;
  queued = true;
  requestAnimationFrame(() => { queued=false; alignAll(); });
}
function watchDensity() {
  if (densityQuery) densityQuery.removeEventListener('change', densityChanged);
  densityQuery = matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`);
  densityQuery.addEventListener('change', densityChanged);
}
function densityChanged() { render(); watchDensity(); }
document.querySelectorAll('[data-scale]').forEach(button => button.addEventListener('click', () => { scale=Number(button.dataset.scale); render(); }));
document.querySelectorAll('[data-theme]').forEach(button => button.addEventListener('click', () => { theme=button.dataset.theme; render(); }));
window.addEventListener('resize', render);
window.addEventListener('scroll', queueAlign, {passive:true,capture:true});
document.fonts.ready.then(() => { render(); watchDensity(); });
</script></html>'''
    (ROOT / 'pixel-accurate.html').write_text(page, encoding='utf-8')
    print('Built eight 96 x 32 bitmaps and pixel-accurate.html.')


if __name__ == '__main__':
    build_page()
