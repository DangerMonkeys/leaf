"""Hand-built one-bit wordmark glyph studies; no font scaling or antialiasing."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import html
import json

ROOT = Path(__file__).resolve().parent
FONT = Path('C:/Windows/Fonts/arial.ttf')
INK = '#202423'
LCD = '#c7cbbf'

def rows(*lines):
    assert len({len(line) for line in lines}) == 1
    return list(lines)

def ell(width, height, stroke):
    if stroke == 1:
        return ['#' + '.' * (width - 1)] * (height - 1) + ['.' + '#' * (width - 1)]
    if width == 3:
        return ['##.'] * (height - 2) + ['###', '.##']
    return ['##..'] * (height - 2) + ['###.', '.###']

def oval(width, height, stroke):
    if stroke == 1:
        return ['.' + '#' * (width - 2) + '.'] + ['#' + '.' * (width - 2) + '#'] * (height - 2) + ['.' + '#' * (width - 2) + '.']
    if width <= 6:
        return ['.' + '#' * (width - 2) + '.'] + ['##' + '.' * (width - 4) + '##'] * (height - 2) + ['.' + '#' * (width - 2) + '.']
    return ['..' + '#' * (width - 4) + '..', '.' + '#' * (width - 2) + '.'] + ['##' + '.' * (width - 4) + '##'] * (height - 4) + ['.' + '#' * (width - 2) + '.', '..' + '#' * (width - 4) + '..']

def gee(width, bowl_height, tail_height, stroke):
    if stroke == 1:
        bowl = ['.' + '#' * (width - 1)] + ['#' + '.' * (width - 2) + '#'] * (bowl_height - 2) + ['.' + '#' * (width - 1)]
        tail = ['.' * (width - 1) + '#'] * (tail_height - 2) + ['.' * (width - 2) + '##', '.' + '#' * (width - 3) + '..']
        return bowl + tail
    if width == 6:
        bowl = ['.#####', '######'] + ['##..##'] * (bowl_height - 4) + ['######', '.#####']
        return bowl + ['....##', '...###', '.####.']
    bowl = ['..' + '#' * (width - 2), '.' + '#' * (width - 1)] + ['##' + '.' * (width - 4) + '##'] * (bowl_height - 4) + ['.' + '#' * (width - 1), '..' + '#' * (width - 2)]
    if tail_height == 3:
        return bowl + ['.' * (width - 2) + '##', '.' * (width - 3) + '###', '.' + '#' * (width - 3) + '..']
    return bowl + ['.' * (width - 2) + '##'] * (tail_height - 3) + ['.' * (width - 3) + '###', '..' + '#' * (width - 3) + '.', '.' + '#' * (width - 3) + '..']

SPECS = [
    dict(id='A', width=20, height=13, lw=3, ow=5, gw=6, gap1=3, gap2=3, top=3, bowl=7, stroke=1, title='Smallest / fine', note='One-pixel strokes; keeps the tall l and wide spacing.'),
    dict(id='B', width=24, height=15, lw=3, ow=6, gw=7, gap1=4, gap2=4, top=3, bowl=9, stroke=2, title='24 / heavier', note='Two-pixel strokes; stronger on the Leaf display, with a tighter o counter.'),
    dict(id='C', width=28, height=18, lw=4, ow=7, gw=8, gap1=5, gap2=4, top=4, bowl=10, stroke=2, title='28 / reference proportions', note='Tall ascender, open o and generous spacing. Best balance of shape and clarity.'),
    dict(id='D', width=30, height=19, lw=4, ow=8, gw=8, gap1=5, gap2=5, top=4, bowl=11, stroke=2, title='30 / most spacious', note='Closest overall proportions; more room for the o and g counters.'),
    dict(id='E', width=24, height=15, lw=3, ow=6, gw=7, gap1=4, gap2=4, top=3, bowl=9, stroke=1, title='24 / fine', note='Same placement as B, with one-pixel strokes for comparison.'),
    dict(id='F', width=24, height=15, lw=4, ow=7, gw=8, gap1=2, gap2=3, top=3, bowl=9, stroke=2, title='24 / wider letters', note='Trades some of the reference spacing for more recognizable letter interiors.'),
    dict(id='G', width=20, height=13, lw=3, ow=5, gw=6, gap1=3, gap2=3, top=3, bowl=7, stroke=2, title='20 / heavier', note='Minimum width with two-pixel strokes; the o becomes very narrow inside.'),
    dict(id='H', width=28, height=15, lw=4, ow=8, gw=8, gap1=4, gap2=4, top=3, bowl=8, stroke=2, title='28 / shorter', note='Lower profile for a dialog heading; broader letters depart from the reference.'),
]

def build(spec):
    w, h, top, bh, stroke = [spec[k] for k in ('width', 'height', 'top', 'bowl', 'stroke')]
    assert spec['lw'] + spec['gap1'] + spec['ow'] + spec['gap2'] + spec['gw'] == w
    glyphs = [ell(spec['lw'], top + bh, stroke), oval(spec['ow'], bh, stroke), gee(spec['gw'], bh, h - top - bh, stroke)]
    mask = Image.new('1', (w, h), 0)
    x = 0
    for i, glyph in enumerate(glyphs):
        y0 = 0 if i == 0 else top
        for y, line in enumerate(glyph):
            for xx, value in enumerate(line):
                if value == '#':
                    mask.putpixel((x + xx, y0 + y), 1)
        if i < 2:
            x += len(glyph[0]) + spec['gap1' if i == 0 else 'gap2']
    assert mask.getbbox() == (0, 0, w, h), (spec['id'], mask.getbbox())
    return mask

def colorize(mask, dark=False):
    image = Image.new('RGB', mask.size, INK if dark else LCD)
    image.paste(LCD if dark else INK, (0, 0), mask)
    return image

def xbm(mask, name):
    w, h = mask.size
    data = []
    for y in range(h):
        for start in range(0, w, 8):
            data.append(sum((1 << bit) for bit in range(8) if start + bit < w and mask.getpixel((start + bit, y))))
    return f'#define {name}_width {w}\n#define {name}_height {h}\nstatic const unsigned char {name}_bits[] = {{\n  ' + ', '.join(f'0x{b:02x}' for b in data) + '\n};\n'

def main():
    (ROOT / 'assets').mkdir(exist_ok=True)
    body = []
    bitmaps = []
    image_cache = []
    for spec in SPECS:
        mask = build(spec)
        stem = f"log-{spec['id'].lower()}-{spec['width']}x{spec['height']}"
        spec['asset'] = 'assets/' + stem + '.png'
        spec['inverse'] = 'assets/' + stem + '-inverse.png'
        light, dark = colorize(mask), colorize(mask, True)
        light.save(ROOT / spec['asset'])
        dark.save(ROOT / spec['inverse'])
        # Save the exact reusable one-bit source, with XBM/LSB-first byte ordering.
        mask.save(ROOT / ('assets/' + stem + '-mask.png'))
        (ROOT / ('assets/' + stem + '.xbm')).write_text(xbm(mask, 'log_' + spec['id'].lower()))
        bitmaps.append(f"{spec['id']}: {spec['width']} x {spec['height']} — {spec['title']}\n" + '\n'.join(''.join('#' if mask.getpixel((x, y)) else '.' for x in range(mask.width)) for y in range(mask.height)))
        image_cache.append((spec, light, dark))
        body.append(f'''<article><h2>{spec['id']} · {spec['title']} <small>{spec['width']} × {spec['height']} px</small></h2><p>{spec['note']}</p>
<div class="native"><span>Actual pixels</span><img src="{spec['asset']}" width="{spec['width']}" height="{spec['height']}" alt="{spec['id']} native size"><img src="{spec['inverse']}" width="{spec['width']}" height="{spec['height']}" alt="{spec['id']} native inverse"></div>
<div class="big"><img src="{spec['asset']}" width="{spec['width']*8}" height="{spec['height']*8}" alt="{spec['id']} eight times"><img src="{spec['inverse']}" width="{spec['width']*8}" height="{spec['height']*8}" alt="{spec['id']} inverse eight times"></div>
<div class="ruler"><span>Across a 96-pixel display:</span><div class="screen"><img src="{spec['asset'].replace('.png', '-frame96x32.png')}" width="96" height="32" alt="{spec['id']} native width context"></div></div></article>''')
    (ROOT / 'pixel-patterns.txt').write_text('\n\n'.join(bitmaps) + '\n', encoding='utf-8')
    (ROOT / 'manifest.json').write_text(json.dumps(SPECS, indent=2) + '\n')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>LOG wordmark pixel studies</title>
<style>body{background:#eef0eb;color:#202423;font:16px/1.45 system-ui,sans-serif;margin:0}header,main{max-width:1240px;margin:auto;padding:24px}h1{margin:0}header p{max-width:950px}main{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px;padding-top:0}article{background:white;border:1px solid #cdd3c8;border-radius:8px;padding:20px;min-width:0}h2{font-size:19px;margin:0}small{font-size:14px;font-weight:400;margin-left:8px}article p{font-size:14px;min-height:42px}.native{display:flex;gap:24px;align-items:center;height:48px}.native span{min-width:90px;font-size:13px}.big{display:flex;justify-content:space-between;align-items:center;gap:16px;height:176px;background:#f7f8f4;padding:12px}img{image-rendering:pixelated;object-fit:contain;flex-shrink:0;max-width:none}.ruler{display:flex;align-items:center;gap:18px;margin-top:18px;font-size:13px}.screen{width:96px;height:28px;background:#c7cbbf;display:flex;align-items:center;justify-content:center;border:1px solid #202423}a{color:#23523c}@media(max-width:1000px){main{grid-template-columns:1fr}}header strong{color:#23523c}</style>
<header><h1>LOG, in Leaf pixels</h1><p>Eight hand-built, one-bit wordmarks. Width is measured from the leftmost ink of the l to the rightmost ink of the g. No added side padding; no antialiasing. Native samples use one image pixel per CSS pixel. Large samples are exactly 8× with nearest-neighbor display.</p><p><strong>Start with C (28 px) and F (24 px).</strong> C preserves more of the reference spacing; F gives the letters more room at a smaller width. A is the cleanest 20 px study. The hooked l, rounded o, flat upper-right g, and leftward descender are retained throughout.</p><p>Reference:</p><img src="reference.png" alt="Original blue lowercase log wordmark" style="width:240px;height:auto;image-rendering:auto"><p><a href="comparison.png">Comparison sheet</a> · <a href="pixel-patterns.txt">Editable pixel patterns</a> · <a href="manifest.json">Sizes and asset names</a></p></header><main>'''+ '\n'.join(body) + '</main></html>'
    (ROOT / 'index.html').write_text(page, encoding='utf-8')
    from make_pixel_page import build_page
    build_page()
    index_path = ROOT / 'index.html'
    original_page = index_path.read_text(encoding='utf-8')
    original_page = original_page.replace('.screen{width:96px;height:28px;background:#c7cbbf;display:flex;align-items:center;justify-content:center;border:1px solid #202423}', '.screen{width:96px;height:32px;display:block;flex:none}.screen img{display:block;width:96px;height:32px}')
    original_page = original_page.replace('<header><h1>', '<header><p><a href="pixel-accurate.html"><strong>Open the pixel-accurate 96-pixel comparison page</strong></a></p><h1>')
    index_path.write_text(original_page, encoding='utf-8')

    title_font = ImageFont.truetype(str(FONT), 27)
    label_font = ImageFont.truetype(str(FONT), 18)
    small_font = ImageFont.truetype(str(FONT), 14)
    sheet = Image.new('RGB', (1160, 1480), '#eef0eb')
    draw = ImageDraw.Draw(sheet)
    draw.text((24, 16), 'LOG · Leaf pixel wordmark studies', font=title_font, fill=INK)
    draw.text((24, 55), 'Native samples are unscaled. Enlarged samples are 8×. Width is ink-to-ink, with no added padding.', font=small_font, fill=INK)
    # Primary size ladder in the left column; alternatives beside it.
    layout = [0, 6, 1, 4, 2, 5, 3, 7]
    for slot, i in enumerate(layout):
        spec, light, dark = image_cache[i]
        x, y = (slot % 2) * 580, 88 + (slot // 2) * 346
        draw.rounded_rectangle((x+10, y, x+570, y+334), radius=10, fill='white')
        draw.text((x+28,y+15), f"{spec['id']}   {spec['title']}", font=label_font, fill=INK)
        draw.text((x+28,y+43), f"{spec['width']} × {spec['height']} pixels", font=small_font, fill=INK)
        draw.text((x+28,y+82), 'Native size:', font=small_font, fill=INK)
        sheet.paste(light,(x+126,y+83))
        sheet.paste(dark,(x+188,y+83))
        for col, image in enumerate([light,dark]):
            sheet.paste(image.resize((image.width*8,image.height*8),Image.Resampling.NEAREST),(x+28+col*270,y+128))
        draw.text((x+28,y+295), '8× pixel detail — positive / inverse', font=small_font, fill=INK)
    sheet.save(ROOT / 'comparison.png', optimize=True)
    print('Generated eight native-size wordmarks, inverse versions, one-bit masks, XBM assets, gallery and comparison sheet.')

if __name__ == '__main__':
    main()
