"""Read-only comparison of conservative landing windows; no firmware changes."""
from pathlib import Path
import json, math, bisect
OUT=Path(__file__).resolve().parent
old=json.loads((OUT/'features.json').read_text())
new=json.loads((OUT/'new-tracks-parsed.json').read_text())
tracks=[]
def hh(t):
 t=round(t);return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'
def distance(a,b):
 return math.hypot((a['lat']-b['lat'])*111195,(a['lon']-b['lon'])*111195*math.cos(math.radians(a['lat'])))
for d in old+new:
 if d in old and 'launch' not in d:continue
 if d in new and len(d['b'])<100:continue
 gs={int(g['T']):g for g in d['gl']}
 ga={int(a['T']):a for a in d['ga']}
 rows=[]
 for b in d['b']:
  if 'ms' not in b:continue
  ms=int(b['ms']);g=gs.get(ms);a=ga.get(ms)
  if not g or not a:continue
  if rows and ms==rows[-1]['ms']:continue
  rows.append(dict(ms=ms,t=ms/1000,local=b['utc']-25200,alt=b['alt'],lat=b['lat'],lon=b['lon'],speed=float(g['S']),raw=float(a.get('R','nan')),valid=b['valid'] and g['SV']=='1' and a.get('RV')=='1'))
 if d in old:
  launch=d['launch'];land=d['land'];label=d['unit']
 else:
  label=d['pilot']+' '+d['name']
  launch=14*3600+3*60+(10 if d['id']==0 else 15)
  land=14*3600+50*60+(15 if d['id']==0 else 5)
 tracks.append(dict(label=label,rows=rows,launch=launch,land=land,new=d in new))
results=[]
for duration in [15,20,30]:
 for max_alt in [2,3]:
  for bubble in [None,5,10]:
   result=dict(window_s=duration,pressure_range_m=max_alt,position_radius_m=bubble,tracks=[])
   for d in tracks:
    rr=d['rows'];tt=[r['t'] for r in rr];hits=[]
    for i,r in enumerate(rr):
     if r['local']<d['launch']+duration:continue
     start=r['t']-duration;j=bisect.bisect_right(tt,start)-1
     if j<0:continue
     window=rr[j:i+1]
     if any(not x['valid'] or x['speed']>=3*.44704 or abs(x['raw']-1)>=.2 for x in window):continue
     if any(b['t']-a['t']>2 for a,b in zip(window,window[1:])):continue
     if max(x['alt'] for x in window)-min(x['alt'] for x in window)>max_alt:continue
     if bubble is not None and max(distance(window[0],x) for x in window)>bubble:continue
     hits.append(r)
    early=[r for r in hits if r['local']<d['land']]
    first=next((r for r in hits if r['local']>=d['land']),None)
    result['tracks'].append(dict(label=d['label'],airborne_checks=len(early),first_airborne=hh(early[0]['local']) if early else None,stop_local=hh(first['local']) if first else None,delay_from_reference_s=first['local']-d['land'] if first else None,recording_ends=hh(rr[-1]['local'])))
   results.append(result)
(OUT/'stop-refinement.json').write_text(json.dumps(results,indent=2))
for r in results:
 if r['pressure_range_m']==3 and r['window_s'] in (15,20,30):
  print('WINDOW',r['window_s'],'ALT',r['pressure_range_m'],'BUBBLE',r['position_radius_m'])
  for t in r['tracks']:print(t)
