from pathlib import Path
import json,csv,math,statistics
OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'parsed.json').read_text())
R=list(csv.DictReader((OUT/'replay-results.csv').open()))
def hh(v):
 t=round(v);return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'
results=[]
for d in D:
 rr=[{k:float(v) for k,v in r.items()} for r in R if int(r['id'])==d['id']]
 byms={int(r['ms']):r for r in d['rows']}
 launch=(13*3600+2*60+5,13*3600+2*60+15) if d['id']==0 else (13*3600+18*60+45,13*3600+19*60+12)
 land=(13*3600+19*60+5,13*3600+19*60+13) if d['id']==0 else (13*3600+29*60+34,13*3600+29*60+38)
 if d['id']==2:launch=land=None
 row=dict(id=d['id'],unit=d['unit'],file=d['name'],start=d['start'],end=d['end'],launch_band=launch,landing_band=land,paths={},ground_hits=[])
 pre=[]
 for r in rr:
  sample=byms[int(r['ms'])]
  if sample['source']=='pre' and int(r['ms'])>d['rows'][0]['ms']+4000:
   pre.append(dict(ms=int(r['ms']),recorded=sample['recorded_triggered'],replayed=int(r['triggered']),recorded_candidates=sample['recorded_candidates'],replayed_candidates=int(r['candidates'])))
  if r['triggered'] and (launch is None or sample['local']<launch[0]-5):row['ground_hits'].append(dict(r,local=hh(sample['local']),source=sample['source']))
 row['precise_prehistory_comparison']=pre
 for bit,label in [(1,'forward'),(2,'vertical'),(4,'blended')]:
  hit=next((r for r in rr if int(r['triggered'])&bit and (launch is None or byms[int(r['ms'])]['local']>=launch[0]-5)),None)
  row['paths'][label]=dict(hit,local=hh(byms[int(hit['ms'])]['local'])) if hit else None
 # Current unchanged auto-stop proxy: integer B pressure altitude cannot recover
 # the actual centimeter anchor. Evaluate it only as an approximation.
 # The speed/acceleration-only count is an upper bound independent of altitude.
 anchor=0;count=0;upper=0;proxy_hits=[];upper_hits=[];last=None;max_air=0;max_air_upper=0
 for s in d['rows']:
  if s['source']!='track':continue
  if last is not None and s['ms']-last>2000:count=upper=0;anchor=s['alt']
  last=s['ms']
  quiet=(not s['flags']&8 or (s['flags']&1 and s['s']<3*.44704)) and s['raw_valid'] and abs(s['raw']-1)<.2
  if quiet:upper+=1
  else:upper=0
  if quiet and abs(s['alt']-anchor)<1:count+=1
  else:count=0;anchor=s['alt']
  if count>=20:proxy_hits.append(dict(local=hh(s['local']),seconds=s['local'],ms=s['ms'],count=count))
  if upper>=20:upper_hits.append(dict(local=hh(s['local']),seconds=s['local'],ms=s['ms'],count=upper))
  if launch and launch[1]<=s['local']<land[0]:max_air=max(max_air,count);max_air_upper=max(max_air_upper,upper)
 after=lambda hits:next((r for r in hits if land is None or r['seconds']>=land[1]),None)
 row['stop_proxy']=dict(first_after_landing=after(proxy_hits),first_possible_from_speed_accel=after(upper_hits),max_inflight_quiet_checks=max_air,max_inflight_speed_accel_checks=max_air_upper,proxy_inflight_hits=sum(launch[1]<=h['seconds']<land[0] for h in proxy_hits) if launch else 0)
 results.append(row)
 print('\n',d['id'],d['unit'],d['name'],'ground qualifying checks',len(row['ground_hits']))
 for k,v in row['paths'].items(): print(k,v)
 print('GROUND HITS',row['ground_hits'][:12])
 print('STOP',row['stop_proxy'])
 print('PRE MATCH',len(pre),sum(x['recorded']==x['replayed'] and x['recorded_candidates']==x['replayed_candidates'] for x in pre))
(OUT/'summary.json').write_text(json.dumps(results,indent=2))
