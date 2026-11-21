"""Analysis-only parameter sweeps over recorded detector windows and stop inputs."""
from pathlib import Path
import json,csv,math
OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'parsed.json').read_text())
R=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((OUT/'replay-results.csv').open())]
LAUNCH={0:46932,1:47942,3:47937,4:47942}
LAND={0:47955,1:48577,3:48577,4:48577}
def hh(v):
 t=round(v);return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'

def stop_bounds(rows,n=20,mph=3,alt_m=1,accel=.2,land=None):
 # All centimeter pressures consistent with each truncated B altitude. A state
 # is the retained altitude anchor plus its consecutive qualifying-check count.
 threshold=round(alt_m*100)
 states={0:{0}};earliest=latest=None;last=None;early=[];max_upper=upper=0
 for s in rows:
  lo=int(s['alt'])*100;hi=lo+99
  if last is not None and s['ms']-last>2000:states={0:set(range(lo,hi+1))};upper=0
  last=s['ms']
  quiet=(not s['flags']&8 or (s['flags']&1 and s['s']<mph*.44704)) and s['raw_valid'] and abs(s['raw']-1)<accel
  upper=upper+1 if quiet else 0
  if land is not None and s['local']<land:max_upper=max(max_upper,upper)
  if not states:continue
  if not quiet:states={0:set(range(lo,hi+1))};continue
  nxt={};fail=set()
  for count,anchors in states.items():
   passing={a for a in anchors if a+threshold-1>=lo and a-threshold+1<=hi}
   if passing:
    if count+1>=n:
     event=dict(local=hh(s['local']),seconds=s['local'],ms=s['ms'])
     if earliest is None:earliest=event
     if land is not None and s['local']<land:early.append(event)
    else:nxt.setdefault(count+1,set()).update(passing)
   fail.update(range(lo,min(hi,max(anchors)-threshold)+1))
   fail.update(range(max(lo,min(anchors)+threshold),hi+1))
  if fail:nxt.setdefault(0,set()).update(fail)
  states=nxt
  if not states and latest is None:latest=dict(local=hh(s['local']),seconds=s['local'],ms=s['ms'])
 return dict(earliest=earliest,latest=latest,possible_airborne_stop=early[0] if early else None,max_airborne_speed_accel_run=max_upper)

stop=[]
configs=[(n,mph,alt) for n in [5,8,10,12,15,20] for mph in [3,4,5,6,8] for alt in [1,2,3]]
for n,mph,alt in configs:
 out=dict(checks=n,speed_mph=mph,altitude_m=alt,tracks=[])
 for d in D:
  if d['id']==2:continue
  rows=[r for r in d['rows'] if r['source']=='track' and r['local']>=LAUNCH[d['id']]]
  res=stop_bounds(rows,n,mph,alt,land=LAND[d['id']]);res['unit']=d['unit'];res['id']=d['id']
  if res['earliest']:res['earliest_delay_s']=res['earliest']['seconds']-LAND[d['id']]
  if res['latest']:res['latest_delay_s']=res['latest']['seconds']-LAND[d['id']]
  out['tracks'].append(res)
 stop.append(out)
(OUT/'stop-threshold-sweep.json').write_text(json.dumps(stop,indent=2))
print('SELECTED STOP OPTIONS')
for r in stop:
 if (r['checks'],r['speed_mph'],r['altitude_m']) in [(20,3,1),(15,3,1),(10,3,1),(15,3,2),(10,3,2),(15,4,2),(10,4,2),(10,5,2),(8,3,1),(5,3,1)]:
  print(json.dumps(r))

def start_hits(rr,mph=5,distance=5,confirmation=1000,scope='shared'):
 # Constants named FORWARD_* also normalize the blended path in the firmware.
 # A standalone-only change must explicitly leave those blended scales at 5/5.
 pending={};hits=[];last=None
 for r in rr:
  ms=int(r['ms']);v=int(r['valid']);z=r['alt_change_m'];c=r['mean_climb_mps'];direction=1 if z>0 else -1
  if last is not None and ms-last>2000:pending={}
  last=ms
  agrees=(v&6)==6 and z*c>0
  vert=min(1,abs(z)/3,abs(c)/.5) if agrees else 0
  forward=(v&9)==9 and r['mean_speed_mps']>=mph*.44704 and r['displacement_m']>=distance
  scale_mph,scale_dist=(mph,distance) if scope=='shared' else (5,5)
  f=min(1,r['mean_speed_mps']/(scale_mph*.44704),r['displacement_m']/scale_dist) if (v&9)==9 else 0
  candidates={1:forward,2:agrees and abs(z)>=3 and abs(c)>.5,4:f>=.7 and vert>=.5 and f+vert*vert>=1}
  mask=0
  for bit,ok in candidates.items():
   sign=0 if bit==1 else direction
   if not ok:pending.pop(bit,None);continue
   if bit not in pending or pending[bit][1]!=sign:pending[bit]=(ms,sign)
   duration=confirmation if bit==1 else 1000
   if ms-pending[bit][0]>=duration:mask|=bit
  if mask:hits.append(dict(ms=ms,mask=mask,window=r))
 return hits

starts=[]
start_configs=[(mph,5,1000,'shared') for mph in [5,5.5,6,6.5,6.8,6.85,6.88,6.89,6.9,7,7.5,8]]
start_configs += [(5,dist,1000,'shared') for dist in [6,7,8,9,10,10.05,10.08,10.09,10.1,10.5,11,12]]
start_configs += [(5,5,ms,'shared') for ms in [2000,3000,4000,4300,4500,5000]]
start_configs += [(7,5,1000,'standalone'),(5,11,1000,'standalone')]
for mph,dist,confirm,scope in start_configs:
 result=dict(speed_mph=mph,distance_m=dist,forward_confirmation_ms=confirm,normalization_scope=scope,tracks=[])
 for d in D:
  rr=[r for r in R if int(r['id'])==d['id']];byms={r['ms']:r for r in d['rows']}
  hits=start_hits(rr,mph,dist,confirm,scope)
  relevant=[h for h in hits if d['id']==2 or byms[h['ms']]['local']>=LAUNCH[d['id']]-20]
  first=relevant[0] if relevant else None
  path={}
  for bit,name in [(1,'forward'),(2,'vertical'),(4,'blended')]:
   h=next((h for h in relevant if h['mask']&bit),None)
   path[name]=hh(byms[h['ms']]['local']) if h else None
  ground=[h for h in hits if d['id']==2 or byms[h['ms']]['local']<LAUNCH[d['id']]-20]
  result['tracks'].append(dict(id=d['id'],unit=d['unit'],ground_checks=len(ground),first=hh(byms[first['ms']]['local']) if first else None,mask=first['mask'] if first else None,paths=path,delay_from_accepted_launch_s=byms[first['ms']]['local']-LAUNCH[d['id']] if first and d['id']!=2 else None))
 starts.append(result)
(OUT/'start-threshold-sweep.json').write_text(json.dumps(starts,indent=2))
print('SELECTED START OPTIONS')
for r in starts:
 if (r['speed_mph'],r['distance_m'],r['forward_confirmation_ms']) in [(5,5,1000),(6.5,5,1000),(6.9,5,1000),(7,5,1000),(5,10,1000),(5,10.1,1000),(5,11,1000),(5,5,4000),(5,5,4500),(5,5,5000)]:print(json.dumps(r))
