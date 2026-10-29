"""Analyze nonlinear launch combinations using independent pressure/position corroboration."""
from pathlib import Path
import json, math
import numpy as np

OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'features.json').read_text());MPH=.44704

def build(d):
    rr=d['rows'];p=sorted((b for b in d['b'] if b.get('valid') and 'ms' in b),key=lambda x:x['ms'])
    pt=np.array([x['ms']/1000 for x in p]);pa=np.array([x['alt'] for x in p]);pla=np.array([x['lat'] for x in p]);plo=np.array([x['lon'] for x in p])
    result=[]
    for i,r in enumerate(rr):
        j=i
        while j>0 and r['t']-rr[j]['t']<3:j-=1
        w=rr[j:i+1];tt=np.array([x['t'] for x in w]);start=tt[-1]-3
        row={'t':r['t'],'local':r['local'],'phase':r['phase'],'speed':None,'climb':None,'gain':None,'displacement':None}
        if tt[-1]-tt[0]>=3 and tt[-1]-tt[0]<4.5 and np.max(np.diff(tt),initial=0)<=2:
            target=np.r_[start,tt[tt>start]]
            for src,key in [('s','speed'),('c','climb')]:
                v=np.array([x[src] for x in w])
                if np.isfinite(v).all():row[key]=float(np.trapezoid(np.interp(target,tt,v),target)/3)
            k=int(np.searchsorted(pt,tt[-1]+1e-6,side='right'));a=int(np.searchsorted(pt,start,side='right'))-1
            if a>=0 and k>a and tt[-1]-pt[k-1]<.3 and np.max(np.diff(pt[a:k]),initial=0)<=2:
                row['gain']=float(pa[k-1]-np.interp(start,pt[a:k],pa[a:k]))
                lat=np.interp(start,pt[a:k],pla[a:k]);lon=np.interp(start,pt[a:k],plo[a:k])
                row['displacement']=math.hypot((pla[k-1]-lat)*111195,(plo[k-1]-lon)*111195*math.cos(math.radians(pla[k-1])))
        result.append(row)
    return result

for d in D:d['combined']=build(d)

def evidence(r,mirror=False):
    if any(r[k] is None for k in ['speed','climb','gain','displacement']):return None
    sign=-1 if mirror else 1
    s=max(0,min(r['speed']/(5*MPH),r['displacement']/5))
    h=max(0,min(sign*r['gain']/3,sign*r['climb']/.5))
    return s,h,sign*r['climb']

def qualifies(r,rule,mirror=False):
    e=evidence(r,mirror)
    if e is None:return False,None
    s,h,c=e
    score=s**rule['speed_power']+h**rule['height_power']
    gates=s>=rule['speed_floor_mph']/5 and h>=rule['height_floor'] and c>rule['climb_floor']
    return gates and score>=1-1e-9,score if gates else None

rules=[]
for sp,hp in [(1,1),(1.5,1.5),(2,1),(1,2)]:
    for speedfloor in [2.5,3,3.25,3.5]:
        for heightfloor in [.25,1/3,.5]:
            for climbfloor in [.15,.2,.25]:
                rules.append({'speed_power':sp,'height_power':hp,'speed_floor_mph':speedfloor,'height_floor':heightfloor,'climb_floor':climbfloor,'confirm_s':1})
results=[]
for rule in rules:
    ground=set();flights={};worst_score=0;worst_sample=None;single_hits=set()
    for d in D:
        for mirror in [False,True]:
            since=None;previous=None
            for r in d['combined']:
                q,score=qualifies(r,rule,mirror)
                if r['phase']=='ground' and score is not None and score>worst_score:
                    worst_score=score;worst_sample={'unit':d['unit'],'track':d['name'],'local':r['local'],'mirrored':mirror,'evidence':evidence(r,mirror)}
                if q and r['phase']=='ground':single_hits.add(d['unit']+'/'+d['name'][-6:])
                if previous is not None and r['t']-previous>2:since=None
                previous=r['t']
                if not q:since=None;continue
                if since is None:since=r['t']
                if r['t']-since<rule['confirm_s']-1e-6:continue
                if r['phase']=='ground':ground.add(d['unit']+'/'+d['name'][-6:])
                if not mirror and 'launch' in d and d['launch']-10<=r['local']<d['land'] and d['unit'] not in flights:
                    flights[d['unit']]=round(r['local']-d['launch'],2)
    results.append({**rule,'ground_files':sorted(ground),'ground_files_before_confirmation':sorted(single_hits),'largest_ground_score_after_gates':round(worst_score,4),'largest_ground_sample':worst_sample,'flight_delay_s':flights})

selected=[r for r in results if r['speed_power']==1 and r['height_power']==2 and r['height_floor']==1/3 and r['climb_floor']==.2]
print('SELECTED')
for r in selected:print(r)
clean=[r for r in results if not r['ground_files']]
print('passing',len(clean),'/',len(results))

aggressive=next(r for r in selected if r['speed_floor_mph']==3.5)
candidate=next(r for r in results if r['speed_power']==1 and r['height_power']==2 and r['speed_floor_mph']==3.5 and r['height_floor']==.5 and r['climb_floor']==.2)
examples=[]
for speed,rate,reported in [(3.75,.5,.25),(3.75,.5,.5),(4,.4,.4),(3.5,.6,.6),(3,.7,.7),(3.75,.25,.25),(4,0,0)]:
    r={'speed':speed*MPH,'displacement':speed*MPH*3,'gain':rate*3,'climb':reported}
    q,score=qualifies(r,candidate)
    examples.append({'speed_mph':speed,'actual_ascent_mps':rate,'mean_reported_vario_mps':reported,'gain_over_3s_m':rate*3,'displacement_over_3s_m':r['displacement'],'evidence':evidence(r),'score':score,'qualifies_before_confirmation':q})
print('EXAMPLES',json.dumps(examples,indent=2))
(OUT/'blended-analysis.json').write_text(json.dumps({'method':'Exact 3-second time-weighted speed/vario, net independent pressure altitude and GPS displacement. Preserve first B fix as separate stream. Inputs absent or >2s gaps invalidate the window. Ground mirrored by negating pressure gain and vario. One-second confirmation on combined predicate. Only available joint histories are evaluable.',
    'accepted_independent_detectors':{'climb':{'gain_m':3,'window_s':3,'mean_climb_strictly_greater_than_mps':.5},'forward':{'mean_speed_mps':5*MPH,'window_s':3,'displacement_m':5,'confirmation_s':1}},
    'candidate':candidate,'less_restrictive_candidate':aggressive,'examples':examples,'sweep':results},indent=2))

linear=next(r for r in results if r['speed_power']==1 and r['height_power']==1 and r['speed_floor_mph']==3.5 and r['height_floor']==1/3 and r['climb_floor']==.2)
text='''Nonlinear blend proposal (analysis only)

Accepted independent tests remain unchanged:
- Climb: >=3m net pressure-altitude gain within 3s AND mean vario >0.5m/s.
- Forward: >=5mph (2.2352m/s) mean speed over 3s AND >=5m net GPS displacement
  in that same window, with one additional second of confirmation.
- Larger departure bubble remains to be specified.

Add a separate mixed test, ORed with those tests; do not replace them.
Use the same complete, valid three-second window for all inputs.

S = max(0, min(mean_speed_mps / 2.2352, net_displacement_m / 5))
U = max(0, min(pressure_altitude_gain_m / 3, mean_vario_mps / 0.5))
Mixed score = S + U*U

Recommended initial guards: S >=0.7 and U >=0.5. Thus both GPS speed >=3.5mph
(1.56464m/s) and net displacement >=3.5m are required; pressure rise >=1.5m
and mean vario >=0.25m/s are required. Require mixed score >=1 and hold the
full predicate for one further elapsed second. Missing data or >2s gaps reset
the confirmation; a single low speed or climb reading changes its rolling mean
rather than wiping the full window. No independent-test threshold is changed.

The speed/displacement minimum prevents reported GPS speed alone supplying
credit. The altitude/vario minimum prevents velocity estimates alone supplying
climb credit, or a pressure spike without supporting vario supplying it.
Only positive pressure gain and positive vario count toward this upward blend.
Downhill reflection is evaluated as a counterfactual negative example, not an
additional descending-flight criterion.

At 75% of forward speed (3.75mph), adequate corresponding GPS displacement,
half the pressure gain (1.5m) and half the standalone vario threshold (0.25m/s):
S=0.75, U=0.5, score=0.75+0.5^2=1.0. This qualifies, including the initial guards.
With steady real ascent 0.5m/s and consistent sensors, it reaches 1.5m in 3s;
one further second of confirmation gives an idealized trigger at about 4s.
This is a constructed scenario, not an observed mixed-launch validation.

The upward term is squared: small upward signals earn relatively little credit.
At U=0.5, it supplies 0.25 score, so speed can drop by 25%. A simple S+U sum
supplies 0.5 credit instead and triggers on two recorded hiking files under
otherwise comparable guards. The nonlinear score matches the stated design
point rather than asserting it is an empirically optimal launch boundary.

For adequate displacement and vario corroboration, recommended minimums are:
3.50mph: gain >=1.644m/3s; mean vario >=0.274m/s.
3.75mph: gain >=1.500m/3s; mean vario >=0.250m/s.
4.00mph: gain >=1.500m/3s; mean vario >=0.250m/s (initial U floor dominates).
Without the conservative U floor, the curved boundary would allow about 1.342m
gain at 4mph and 1.000m gain at 4.5mph. Those weaker cases are intentionally
left out of the recommended initial mixed path pending higher-resolution data.

Observed results: the recommended test gives no recorded ground triggers in
the jointly evaluable portions of all units, including vertical reflections.
No ground window even met its two evidence floors. A less restrictive U>=1/3
variant also had no triggers, with largest eligible ground score 0.9051. Its
closest case was BlueGreen326 10: S~0.794, recorded pressure gain 1m and mean
vario ~0.417m/s. A true gain around 1.36m would cross the unrestricted curve;
a true gain >=1.5m would meet the recommended floor and cross the curve.
Both could be hidden inside whole-meter IGC rounding. Thus the recordings do
not establish that the proposed firmware test would have zero false positives
with its full-resolution sensor inputs. The conservative guard gives some
additional margin but does not resolve this measurement uncertainty.

Pre-start pressure and position history are missing, and climb validity after
start is only partly observable. B altitude and position are quantized. Ground
classification and transition exclusions follow the initial analysis. The
velocity and pressure measurements are corroborating signals, not statistically
independent evidence. Mirrored hiking checks negate pressure gain and vario
together, retaining horizontal motion. All 144 candidate combinations use
available windows; this sweep is fitting, not independent validation.

The recorded launches were already detected earlier by the accepted forward
test: the mixed branch alone first qualifies much later (roughly 12-35s). An
OR combination therefore leaves those launch times unchanged. Its proposed
benefit is the hypothetical slow, climbing launch that is absent from these
recordings; no observed improvement for that case can be claimed. Confirming
the benefit needs a real mixed launch or a faithful simulator scenario, plus
centimeter-resolution pressure history captured while idle to check ground
separation. No firmware, build, flashing or original recording changes.

Firmware units: the project already uses gps.speed.mps(), so m/s is available
without changing the GPS parser. Preserve exact 5mph=2.2352m/s and
3.5mph=1.56464m/s (rounding to 2.24/1.56 changes the thresholds slightly).
'''
(OUT/'blended-analysis.txt').write_text(text,encoding='utf-8')
