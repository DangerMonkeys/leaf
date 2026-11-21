from pathlib import Path
import ast, json, re, math, hashlib, statistics, collections, csv
OUT=Path(__file__).resolve().parent
ROOT=OUT.parent
old=ROOT.parent/'2026-10-25-firmware-0.2.4-968/_motion-analysis/analyze.py'
tree=ast.parse(old.read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('fields','number','parse','hh')],type_ignores=[]),'parser','exec'))
tracks=[];inventory=[]
for p in sorted(ROOT.rglob('*.igc')):
 text=p.read_text(encoding='utf-8',errors='replace')
 date=next((x[5:11] for x in text.splitlines() if x.startswith('HFDTE')),'')
 inventory.append(dict(path=p.relative_to(ROOT).as_posix(),date=date))
 if date!='271026':continue
 d=parse(p);d['id']=len(tracks);d['unit']=p.relative_to(ROOT).parts[0]
 d['pilot']=next((x.split(':',1)[1] for x in text.splitlines() if x.startswith('HFPLT')),'Unknown')
 extras={}
 for line in text.splitlines():
  for prefix,key in [('LXLVSTARTALT ','alt'),('LXLVSTARTPOS ','pos'),('LXLVSTARTPATH ','path')]:
   if line.startswith(prefix):
    x=fields(line);extras.setdefault(int(x['DT_MS']),{})[key]=x
 start=int(d['start']['T_MS']);first=next(b for b in d['b'] if 'ms' in b)
 d['offset']=first['utc']-25200-first['ms']/1000
 gs={int(g['T']):g for g in d['gl']};ga={int(g['T']):g for g in d['ga']}
 rows=[]
 for x in d['pre']:
  dt=int(x['DT_MS']);e=extras.get(dt,{});f=int(x['F'],16)
  rows.append(dict(ms=start+dt,s=float(x['V']) if f&1 else 0,c=float(x['C']) if f&2 else 0,alt=float(e.get('alt',{}).get('A',0)),lat=float(e.get('pos',{}).get('LAT',0)),lon=float(e.get('pos',{}).get('LON',0)),flags=(f&3)|(4 if f&16 and 'alt' in e else 0)|(8 if f&32 and 'pos' in e else 0),source='pre',original_flags=f,recorded_candidates=int(e.get('path',{}).get('Q','0'),16),recorded_triggered=int(e.get('path',{}).get('T','0'),16)))
 for b in d['b']:
  if 'ms' not in b:continue
  ms=int(b['ms']);g=gs.get(ms);a=ga.get(ms,{})
  if not g:continue
  rows.append(dict(ms=ms,s=float(g['S']),c=b.get('VAR',0)/10,alt=b['alt'],lat=b['lat'],lon=b['lon'],flags=(int(g['SV']=='1'))|(2*int(g['CV']=='1' and 'VAR' in b))|4|(8*int(b['valid'])),source='track',raw=float(a.get('R','nan')),raw_valid=a.get('RV')=='1',local=b['utc']-25200))
 rows.sort(key=lambda r:r['ms'])
 # Both streams are kept: the final precise START sample can be immediately
 # followed by the first rounded B/GL sample. Deduplicate only identical times.
 rows=list({r['ms']:r for r in rows}.values())
 for r in rows:r.setdefault('local',r['ms']/1000+d['offset'])
 d['rows']=rows
 d['header_config']=[line for line in text.splitlines() if line.startswith(('LXLVSTARTCFG','LXLVSTARTFORWARD','LXLVSTARTVERT','LXLVSTARTBLEND'))]
 tracks.append(d)
(OUT/'inventory.json').write_text(json.dumps(inventory,indent=2))
(OUT/'parsed.json').write_text(json.dumps(tracks))
with (OUT/'replay-input.txt').open('w') as f:
 for d in tracks:
  for r in d['rows']:
   f.write(' '.join(str(x) for x in [d['id'],r['ms'],r['s'],r['c'],r['alt'],r['lat'],r['lon'],r['flags']])+'\n')
print('Inventory',len(inventory),'IGCs; dates:',collections.Counter(x['date'] for x in inventory))
for d in tracks:
 rr=[r for r in d['rows'] if r['source']=='track']
 print('\nTRACK',d['id'],d['unit'],d['name'],d['firmware'],'B',len(d['b']),'GL',len(d['gl']),'START',d['start'],'END',d['end'],'local',hh(rr[0]['local']),hh(rr[-1]['local']))
 print('Precise prehistory:',len(d['pre']),'complete',sum(r['flags']==15 for r in d['rows'] if r['source']=='pre'))
 print('Minute: min/mean/max mph, pressure min/max, GPS altitude min/max')
 groups={}
 for r in rr:groups.setdefault(int(r['local']//60),[]).append(r)
 for minute,ss in groups.items():
  print(hh(minute*60),*[round(fn(x['s']/.44704 for x in ss),1) for fn in [min,statistics.mean,max]],min(x['alt'] for x in ss),max(x['alt'] for x in ss))
