"""Read-only field-track analysis. All generated outputs stay beside this script."""
from pathlib import Path
import re, json, csv, math, hashlib
from collections import Counter
import numpy as np

OUT = Path(__file__).resolve().parent
ROOT = OUT.parent

def fields(s):
    return dict(re.findall(r'(\w+)=([^ ]+)', s))

def number(d,k,default=float('nan')):
    try: return float(d[k])
    except (KeyError,ValueError): return default

def parse(p):
    text=p.read_text(errors='replace'); lines=text.splitlines()
    d={'path':p.relative_to(ROOT).as_posix(),'unit':p.parts[-3], 'hardware':p.parts[-4],
       'name':p.name, 'sha256':hashlib.sha256(p.read_bytes()).hexdigest(), 'start':{},'end':{},'pre':[],'b':[],'gl':[],'ga':[]}
    ext={}; last_b=None
    for line in lines:
        if line.startswith('HFDTE'):
            match=re.search(r'(\d{6})',line[5:])
            if match:d['date']=match.group(1)
        if line.startswith('HFRFW'): d['firmware']=line.split(':',1)[-1]
        if line.startswith('I'):
            for j in range(int(line[1:3])):
                a=3+7*j; ext[line[a+4:a+7]]=(int(line[a:a+2])-1,int(line[a+2:a+4]))
        if line.startswith('B'):
            try:
                t=int(line[1:3])*3600+int(line[3:5])*60+int(line[5:7])
                lat=(int(line[7:9])+int(line[9:14])/60000)*(1 if line[14]=='N' else -1)
                lon=(int(line[15:18])+int(line[18:23])/60000)*(1 if line[23]=='E' else -1)
                b={'utc':t,'lat':lat,'lon':lon,'valid':line[24]=='A','alt':int(line[25:30]),'gpsalt':int(line[30:35])}
                for k,(a,z) in ext.items(): b[k]=float(line[a:z])
                d['b'].append(b); last_b=b
            except (ValueError,IndexError): d.setdefault('bad_b',0); d['bad_b']+=1
        for prefix,key in [('LXLVSTART ', 'start'),('LXLVEND ','end'),('LXLVSTARTCFG ','config')]:
            if line.startswith(prefix): d[key]=fields(line)
        if line.startswith('LXLVSTARTDATA '): d['pre'].append(fields(line))
        if line.startswith('LXLVGL '):
            g=fields(line); d['gl'].append(g)
            if last_b is not None and 'ms' not in last_b: last_b['ms']=number(g,'T')
        if line.startswith('LXLVGA '): d['ga'].append(fields(line))
    return d

def hh(t):
    t=int(t)%86400; return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'

def dist(lat,lon):
    lat=np.radians(lat);lon=np.radians(lon)
    return 6371000*2*np.arcsin(np.minimum(1,np.sqrt(np.sin(np.diff(lat)/2)**2+np.cos(lat[:-1])*np.cos(lat[1:])*np.sin(np.diff(lon)/2)**2)))

tracks=[]; inventory=[]
for p in sorted(ROOT.glob('hardware-*/*/tracks/*')):
    if p.suffix.lower()!='.igc': continue
    d=parse(p); inventory.append({k:d.get(k) for k in ['path','unit','date','firmware','sha256']})
    if '2026-10-25' not in p.name and d.get('date')!='251026': continue
    b=d['b']; g=d['gl']; t=np.array([number(x,'T')/1000 for x in g]); s=np.array([number(x,'S') for x in g]); c=np.array([number(x,'C')/100 for x in g]);
    bs=np.array([x.get('GSP',float('nan'))/3.6 for x in b]); al=np.array([x['alt'] for x in b]);
    row={k:d.get(k) for k in ['path','unit','hardware','name','date','firmware','sha256']}
    row.update(n_b=len(b),n_gl=len(g),cause=d['start'].get('CAUSE','unknown'),end=d['end'].get('CAUSE','missing'),duration=round((t[-1]-t[0]) if len(t) else 0,2),
               local_start=hh(b[0]['utc']-7*3600) if b else '',local_end=hh(b[-1]['utc']-7*3600) if b else '',
               alt_start=int(al[0]) if len(al) else None, alt_end=int(al[-1]) if len(al) else None,alt_range=int(np.ptp(al)) if len(al) else None,
               max_speed=round(float(np.nanmax(s)),3) if len(s) else None,p95_speed=round(float(np.nanpercentile(s,95)),3) if len(s) else None,
               climb_p95=round(float(np.nanpercentile(c,95)),3) if len(c) else None,sink_p05=round(float(np.nanpercentile(c,5)),3) if len(c) else None,
               distance=round(float(np.sum(dist([x['lat'] for x in b],[x['lon'] for x in b])))) if b else 0,
               max_gap=round(float(np.max(np.diff(t))),3) if len(t)>1 else 0)
    d['summary']=row; tracks.append(d)

OUT.mkdir(exist_ok=True)
(OUT/'parsed.json').write_text(json.dumps(tracks,allow_nan=True))
(OUT/'inventory.json').write_text(json.dumps(inventory,indent=2))
with (OUT/'tracks.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=tracks[0]['summary'].keys());w.writeheader();w.writerows(d['summary'] for d in tracks)
print('All IGC headers:',Counter(x['date'] for x in inventory))
print('Selected',len(tracks),'tracks',Counter(d['unit'] for d in tracks))
for d in tracks:
    r=d['summary']; print(r['unit'],r['name'][-6:],r['local_start'],r['local_end'],r['cause'],r['end'],r['duration'],'s',r['n_b'],'B',r['alt_start'],r['alt_end'],'alt',r['max_speed'],r['p95_speed'],'m/s',r['distance'],'m',r['max_gap'],'gap')
