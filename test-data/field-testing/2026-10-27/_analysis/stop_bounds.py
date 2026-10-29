"""Threshold replay over every centimeter value allowed by truncated B altitude.

Not exact device reconstruction: post-start freshness, cancellations and pressure
fractions were not logged. Bounds assume each B/GL sample represents its stop
check and use the current 20-check counter, not a redesigned elapsed-time stop.
"""
from pathlib import Path
import json
OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'parsed.json').read_text())
S=json.loads((OUT/'summary.json').read_text())
def hh(v):
 t=round(v);return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'
def bounds(rows):
 states={0:{0}};earliest=latest=None;last=None
 for s in rows:
  lo=int(s['alt'])*100;hi=lo+99
  if last is not None and s['ms']-last>2000:states={0:set(range(lo,hi+1))}
  last=s['ms']
  quiet=(not s['flags']&8 or (s['flags']&1 and s['s']<3*.44704)) and s['raw_valid'] and abs(s['raw']-1)<.2
  if not quiet:states={0:set(range(lo,hi+1))};continue
  next_states={};fail=set()
  for count,anchors in states.items():
   passing={a for a in anchors if a+99>=lo and a-99<=hi}
   if passing:
    if count+1>=20:
     if earliest is None:earliest=dict(local=hh(s['local']),seconds=s['local'],ms=s['ms'])
    else:next_states.setdefault(count+1,set()).update(passing)
   # A failing altitude sample becomes the new anchor. Union all possible
   # samples >=100 cm from at least one currently possible anchor.
   fail.update(range(lo,min(hi,max(anchors)-100)+1))
   fail.update(range(max(lo,min(anchors)+100),hi+1))
  if fail:next_states.setdefault(0,set()).update(fail)
  states=next_states
  if not states:
   latest=dict(local=hh(s['local']),seconds=s['local'],ms=s['ms']);break
 return dict(earliest_possible=earliest,latest_across_reconstructions=latest)
result=[]
for d,s in zip(D,S):
 rows=[r for r in d['rows'] if r['source']=='track']
 launch=s['launch_band']
 x=dict(id=d['id'],unit=d['unit'],file=d['name'],from_timer_start=bounds(rows),from_launch=bounds([r for r in rows if r['local']>=launch[0]]) if launch else None)
 result.append(x);print(json.dumps(x))
(OUT/'stop-bounds.json').write_text(json.dumps(result,indent=2))
