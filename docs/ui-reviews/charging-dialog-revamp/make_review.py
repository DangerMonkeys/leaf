"""Review actual simulator frames at native size and exact physical-pixel zoom."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import base64
import json

ROOT = Path(__file__).resolve().parent
GROUPS = [
    ('Checking flights', [f'scanning-{i}' for i in range(7)], 51),
    ('Starting WiFi', [f'wifi-{i}' for i in range(4)], 51),
    ('Connecting', [f'connecting-{i}' for i in range(4)], 51),
    ('Uploading / preparing', [f'uploading-{i}' for i in range(11)], 51),
    ('Paused — USB hidden', [f'paused-{i}' for i in range(4)], 51),
    ('Cancel confirmation', ['cancel-confirmation'], 51),
    ('Batch processed', ['complete-9-of-9'], 51),
    ('100-flight limit', ['complete-100-of-100'], 51),
    ('USB available', ['usb-available'], 23),
    ('Closing USB', ['closing-usb'], 23),
    ('Turning on', ['turning-on'], 23),
    ('USB not ready', ['usb-unavailable'], 23),
    ('No SD card', ['no-sd-card'], 12),
]


def main():
    for file in (ROOT / 'screenshots').glob('*.png'):
        image = Image.open(file).convert('RGB')
        assert image.size == (96, 192), file
        original = image.tobytes()
        image.save(file, optimize=True)
        assert Image.open(file).tobytes() == original
    groups = []
    cards = []
    for i, (label, files, height) in enumerate(GROUPS):
        images = [Image.open(ROOT / 'screenshots' / (file + '.png')).convert('RGB') for file in files]
        gif = f'screenshots/{files[0]}-animated.gif'
        if len(images) > 1:
            images[0].save(ROOT / gif, save_all=True, append_images=images[1:], duration=500, loop=0, optimize=False)
        y_top = 141 if height == 51 else (150 if height == 23 else 180)
        groups.append(dict(label=label, height=height, y=y_top, files=files,
                           images=['data:image/png;base64,' + base64.b64encode((ROOT / 'screenshots' / (file + '.png')).read_bytes()).decode() for file in files]))
        cards.append(f'<article><h2>{label}</h2><p>{len(files)} frame' + ('s' if len(files) != 1 else '') + f' · 500 ms per frame · 96 × {height} ' + ('footer' if height==12 else 'dialog') + f'</p><div class="previews"><div class="slot" data-group="{i}" data-kind="native"><canvas aria-label="{label}, native screen"></canvas></div><div class="slot" data-group="{i}" data-kind="dialog"><canvas aria-label="{label}, enlarged dialog"></canvas></div></div><a href="screenshots/{files[0]}.png">Native 96 × 192 PNG</a></article>')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Leaf charging dialog revamp</title>
<style>*{box-sizing:border-box}body{margin:0;color:#202423;background:#eef0eb;font:15px/1.5 system-ui,sans-serif}header{padding:24px;max-width:1150px}h1{margin:0 0 10px;font-size:28px}header p{max-width:1000px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,530px),1fr));gap:18px;padding:0 24px 24px;max-width:1500px}article{background:white;border:1px solid #d0d6c9;border-radius:8px;padding:18px;min-width:0}h2{font-size:19px;margin:0}article p{font-size:13px;margin:5px 0 18px;color:#596452}.previews{display:flex;align-items:flex-start;gap:24px;overflow-x:auto;padding-bottom:8px;margin-bottom:12px}.slot{position:relative;flex:none;contain:layout paint}canvas{position:absolute;display:block;max-width:none;image-rendering:pixelated}button{font:inherit;padding:6px 12px;background:white;border:1px solid #a5b19c;border-radius:5px;cursor:pointer}button[aria-pressed=true]{background:#202423;color:white}.controls{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-top:18px}a{color:#23523c;font-size:13px}.status{font-size:13px}</style>
<header><h1>Leaf charging dialogs · revamp</h1><p>Recorded simulator output using the real firmware renderer. Full screens on the left are native size; the dialog on the right is enlarged at an exact integer scale. Both are aligned to physical display pixels, accounting for zoom and display density.</p><p>Leaf Log: 96 × 51 at y=141. USB/power: 96 × 22 at y=141. Two taps confirm cancellation within five seconds; uploads continue during the question. Paused animation stays visible while USB is hidden. USB availability stays visible while the drive is exposed. Each session handles at most 100 eligible flights.</p><div class="controls"><button id="play">Pause animations</button><button id="step">Next frame</button><span>Dialog zoom:</span><button data-scale="2" aria-pressed="false">2×</button><button data-scale="4" aria-pressed="true">4×</button><button data-scale="8" aria-pressed="false">8×</button></div><p class="status" id="status"></p><p><a href="reference/dialog-spec.png">Original reference</a> · <a href="overview.png">Overview sheet</a></p></header><main>''' + '\n'.join(cards) + '''</main><script>
const groups=DATA;let zoom=4,frame=0,playing=true,queued=false,densityQuery;
const slots=[...document.querySelectorAll('.slot')];
function align(){const d=devicePixelRatio||1;slots.forEach(s=>{const b=s.getBoundingClientRect(),c=s.querySelector('canvas');c.style.left=(Math.ceil(b.left*d-1e-7)/d-b.left)+'px';c.style.top=(Math.ceil(b.top*d-1e-7)/d-b.top)+'px';});}
function draw(){const d=devicePixelRatio||1;slots.forEach(s=>{const g=groups[Number(s.dataset.group)],native=s.dataset.kind==='native',scale=native?1:zoom,h=native?192:g.height,c=s.querySelector('canvas');c.width=96*scale;c.height=h*scale;c.style.width=(c.width/d)+'px';c.style.height=(c.height/d)+'px';s.style.width=(c.width/d+2)+'px';s.style.height=(c.height/d+2)+'px';const ctx=c.getContext('2d',{alpha:false});ctx.imageSmoothingEnabled=false;ctx.drawImage(g.loaded[frame%g.loaded.length],0,native?0:141,96,h,0,0,c.width,c.height);});align();document.getElementById('status').textContent=`Native: 1 screen pixel per Leaf pixel · Enlarged: ${zoom} × ${zoom} screen pixels per Leaf pixel · display density ${(devicePixelRatio||1).toFixed(2)}`;document.querySelectorAll('[data-scale]').forEach(b=>b.setAttribute('aria-pressed',String(Number(b.dataset.scale)===zoom)));}
function scheduleAlign(){if(queued)return;queued=true;requestAnimationFrame(()=>{queued=false;align()});}
function densityChanged(){draw();watchDensity();}function watchDensity(){if(densityQuery)densityQuery.removeEventListener('change',densityChanged);densityQuery=matchMedia(`(resolution: ${devicePixelRatio||1}dppx)`);densityQuery.addEventListener('change',densityChanged);}
Promise.all(groups.map(async g=>{g.loaded=await Promise.all(g.images.map(src=>new Promise(resolve=>{const image=new Image();image.onload=()=>resolve(image);image.src=src;})));})).then(()=>document.fonts.ready).then(()=>{draw();watchDensity();setInterval(()=>{if(playing){frame++;draw();}},500);window.addEventListener('resize',draw);window.addEventListener('scroll',scheduleAlign,{capture:true,passive:true});document.getElementById('play').onclick=()=>{playing=!playing;document.getElementById('play').textContent=playing?'Pause animations':'Play animations';};document.getElementById('step').onclick=()=>{playing=false;document.getElementById('play').textContent='Play animations';frame++;draw();};document.querySelectorAll('[data-scale]').forEach(b=>b.onclick=()=>{zoom=Number(b.dataset.scale);draw();});});
</script></html>'''.replace('DATA', json.dumps(groups, separators=(',', ':')))
    page = page.replace('USB/power: 96 × 22 at y=141.', 'USB/power: 96 × 23 at y=150, extended upward one pixel for equal text margins. Leaf Log interaction artwork and counts are raised one pixel. Paused animation cycles through zero, one, two, three dots. A missing card replaces the version with an SD icon and a 6 × 12 NO SD CARD! warning.')
    page = page.replace('native?0:141', 'native?0:g.y')
    (ROOT / 'review.html').write_text(page, encoding='utf-8')
    columns=5
    sheet=Image.new('RGB',(columns*220,((len(GROUPS)+columns-1)//columns)*452+75),'#eef0eb')
    draw=ImageDraw.Draw(sheet)
    font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',16)
    title=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',26)
    draw.text((18,15),'Leaf charging dialog revamp · actual simulator output',font=title,fill='#202423')
    for i,(label,files,height) in enumerate(GROUPS):
        x,y=(i%columns)*220,75+(i//columns)*452
        draw.rounded_rectangle((x+5,y,x+215,y+438),radius=7,fill='white')
        draw.text((x+13,y+12),label,font=font,fill='#202423')
        image=Image.open(ROOT/'screenshots'/(files[0]+'.png'))
        sheet.paste(image.resize((192,384),Image.Resampling.NEAREST),(x+14,y+44))
    sheet.save(ROOT/'overview.png',optimize=True)
    print(f'Review page created from {len(list((ROOT/"screenshots").glob("*.png")))} verified native 96x192 simulator PNGs; five animations use exact 500 ms frames.')


if __name__=='__main__':main()
