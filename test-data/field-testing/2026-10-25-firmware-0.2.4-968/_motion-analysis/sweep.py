from pathlib import Path
import json, math
import numpy as np
OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'features.json').read_text())

def replay(rows, speed=4.2,climb=.7,memory=2.4, mirror=False):
    score=0; previous=None;observed=0;hits=[];max_ground=0
    maximum=1/(1-math.exp(-3/memory))
    for r in rows:
        s=r['s']/.44704;c=-r['c'] if mirror else r['c'];t=r['t']
        if not math.isfinite(s) and not math.isfinite(c):score=0;previous=None;observed=0;continue
        if previous is None:previous=t;continue
        dt=t-previous;previous=t
        if dt>2:score=0;observed=0;continue
        observed=min(3,observed+dt)
        motion=(max(0,s)/speed if math.isfinite(s) else 0)+(max(0,c)/climb if math.isfinite(c) else 0)
        target=min(maximum,motion); retention=math.exp(-dt/memory);score=target+(score-target)*retention
        if r['phase']=='ground':max_ground=max(max_ground,score)
        if motion>1 and observed>=3-1e-8 and score>=1-1e-6: hits.append(r)
    return hits,max_ground

out=[]
for speed in [6,7,8,9,10,12]:
 for climb in [1,1.5,2,2.5,3,4,float('inf')]:
  for memory in [2.4,4,6]:
   ground=set();worst=0; launches={}
   for d in D:
    for mirror in [False,True]:
     hit,mx=replay(d['rows'],speed,climb,memory,mirror)
     if any(r['phase']=='ground' for r in hit):ground.add(d['unit']+'/'+d['name'][-6:])
     worst=max(worst,mx)
     if not mirror and 'launch' in d:
      h=[r for r in hit if r['local']>=d['launch']-10]
      launches[d['unit']]=round(h[0]['local']-d['launch'],1) if h else None
   out.append({'speed_mph':speed,'climb_mps':climb if math.isfinite(climb) else None,'memory_s':memory,'ground_tracks_either_direction':sorted(ground),'ground_max_score':round(worst,4),'delay_s':launches})
(OUT/'sweep.json').write_text(json.dumps(out,indent=2))
clean=[x for x in out if not x['ground_tracks_either_direction'] and all(v is not None for v in x['delay_s'].values())]
clean.sort(key=lambda x:(max(x['delay_s'].values()),sum(x['delay_s'].values())))
print('Passing combinations',len(clean),'/',len(out))
for x in clean[:12]:print(x)
print('Existing replay ground hits (up/down):')
for d in D:
    counts=[]
    for mirror in [False,True]:
        hits,_=replay(d['rows'],mirror=mirror);counts.append(sum(r['phase']=='ground' for r in hits))
    print(d['unit'],d['name'][-6:],counts)
print('Selected comparisons')
for x in out:
    if x['speed_mph'] in [8,9,10] and x['climb_mps'] in [2,3,None] and x['memory_s']==2.4:print(x)
