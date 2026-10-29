"""Analyze responsive forward-start candidates; preserve original recordings."""
from pathlib import Path
import json, math
import numpy as np

OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'features.json').read_text())
MPH=.44704

def hh(t):
    t=round(t);return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'

def features(rows,w,positions):
    positions=sorted((p for p in positions if p.get('valid') and 'ms' in p),key=lambda p:p['ms'])
    pt=np.array([p['ms']/1000 for p in positions]);pla=np.array([p['lat'] for p in positions]);plo=np.array([p['lon'] for p in positions])
    result=[]
    for i,r in enumerate(rows):
        j=i
        while j>0 and r['t']-rows[j]['t']<w:j-=1
        win=rows[j:i+1];tt=np.array([x['t'] for x in win]);dt=tt[-1]-tt[0]
        if dt<w or dt>w+1.5 or np.max(np.diff(tt),initial=0)>2:continue
        speed=np.array([x['s'] for x in win])
        if not np.isfinite(speed).all():continue
        start=tt[-1]-w;t=np.r_[start,tt[tt>start]];v=np.interp(t,tt,speed)
        mean=float(np.trapezoid(v,t)/w);pos=None
        # Keep positions as an independent stream: STARTDATA and the first B fix
        # can share a speed-check second, and speed de-duplication must not erase
        # that first position. Never use a future position at a pre-start check.
        k=int(np.searchsorted(pt,tt[-1]+1e-6,side='right'))
        jpos=int(np.searchsorted(pt,start,side='right'))-1
        if jpos>=0 and k>jpos and tt[-1]-pt[k-1]<.3 and np.max(np.diff(pt[jpos:k]),initial=0)<=2:
            lat=np.interp(start,pt[jpos:k],pla[jpos:k]);lon=np.interp(start,pt[jpos:k],plo[jpos:k]);nowlat=pla[k-1];nowlon=plo[k-1]
            pos=math.hypot((nowlat-lat)*111195,(nowlon-lon)*111195*math.cos(math.radians(nowlat)))
        result.append({'row':r,'mean_mph':mean/MPH,'minimum_mph':float(min(v))/MPH,'distance_m':pos,'window':w})
    return result

for d in D:d['forward']={w:features(d['rows'],w,d['b']) for w in [2,3,4,5]}

rules=[{'name':'Old reference: every reading >8 mph for 5 s','window':5,'kind':'min','mph':8,'distance':None,'confirm_s':0}]
for w in [2,3,4]:
    for speed in [5,5.5,6,7,8]:
        for distance in [None,5,6]:
            rules.append({'name':f'{w}s mean >= {speed}mph'+(f' AND net displacement >= {distance}m' if distance else ''),
                          'window':w,'kind':'mean','mph':speed,'distance':distance,'confirm_s':0})
for speed in [5,5.5,6]:
    for distance in [5,6]:
        for dwell in [1,2]:
            rules.append({'name':f'3s mean >= {speed}mph AND net displacement >= {distance}m; confirm {dwell}s',
                          'window':3,'kind':'mean','mph':speed,'distance':distance,'confirm_s':dwell})
results=[]
for rule in rules:
    by_track=[]
    for d in D:
        hits=[];since=None;last=None
        for x in d['forward'][rule['window']]:
            valid=x['minimum_mph']>rule['mph'] if rule['kind']=='min' else x['mean_mph']>=rule['mph']
            if rule['distance'] is not None:valid=valid and x['distance_m'] is not None and x['distance_m']>=rule['distance']
            if last is not None and x['row']['t']-last>2:since=None
            last=x['row']['t']
            if valid:
                if since is None:since=x['row']['t']
                if x['row']['t']-since>=rule['confirm_s']-1e-6:hits.append(x)
            else:since=None
        ground=[x for x in hits if x['row']['phase']=='ground']
        first=next((x for x in hits if 'launch' in d and d['launch']-10<=x['row']['local']<d['land']),None)
        offset=float(np.median([r['local']-r['t'] for r in d['rows']]))
        first_pressure=min((p['ms']/1000+offset for p in d['b'] if 'ms' in p and p.get('valid')),default=None)
        by_track.append({'unit':d['unit'],'track':d['name'],'ground_windows':len(ground),
          'first_launch_PDT':hh(first['row']['local']) if first else None,
          'launch_delay_s':round(first['row']['local']-d['launch'],2) if first else None,
          'first_recorded_position_delay_s':round(first_pressure-d['launch'],2) if 'launch' in d and first_pressure is not None else None})
    results.append({**rule,'ground_files':[x['unit']+'/'+x['track'][-6:] for x in by_track if x['ground_windows']],
                    'flight_results':[x for x in by_track if x['launch_delay_s'] is not None]})

maxima=[]
for cat in ['hiking','launch ground','Bethany anomaly','post-landing']:
    rr=[]
    for d in D:
        for x in d['forward'][3]:
            r=x['row']
            if r['phase']!='ground':continue
            group='hiking' if d['category'].startswith('Hiking') else ('Bethany anomaly' if d['unit']=='Bethany' and 'launch' not in d else ('post-landing' if 'land' in d and r['local']>d['land'] else 'launch ground'))
            if group==cat:rr.append(x)
    fast=[x for x in rr if x['mean_mph']>=5 and x['distance_m'] is not None]
    maxima.append({'group':cat,'max_3s_mean_mph':max((x['mean_mph'] for x in rr),default=None),
                   'max_3s_distance_when_mean_at_least_5mph_m':max((x['distance_m'] for x in fast),default=None)})

# Ideal fixed-location GPS speed-glitch vs actual motion at the same reported speeds.
# Three-second history is sampled at 1 Hz; constant straight movement must satisfy
# BOTH conditions, and a 6->4->6 mph dip must retain evidence rather than reset.
synthetic=[]
for v in [5,5.5,6,8]:
    synthetic.append({'speed_mph':v,'distance_over_3s_m':3*v*MPH,'expected_constant_speed_trigger_s':4 if v>=5 and 3*v*MPH>=5 else None})

result={'accepted_climb_plan':{'pressure_gain_m':3,'window_s':3,'mean_climb_mps_strictly_greater_than':.5},
 'method':'Speed from exact STARTDATA prehistory and GL diagnostics during recording. Time-weighted trapezoidal averages at exact rolling-window endpoints. Net displacement from a separate stream of all valid recorded B positions, not integrated reported speed. This preserves the first B fix even when speed-check de-duplication keeps STARTDATA for the same second. Missing/invalid speed or gaps >2s invalidate a window. No pre-start positions are present, so early positional detection is censored.',
 'maxima':maxima,'synthetic_constant_speed':synthetic,'rules':results}
(OUT/'forward-analysis.json').write_text(json.dumps(result,indent=2))
print('MAXIMA',json.dumps(maxima,indent=2))
selected=[x for x in results if x['kind']=='min' or (x['window']==3 and x['mph'] in [5,5.5,6,8] and x['distance'] in [None,5])]
for x in selected:print(x)

lines=['Forward start analysis','', 'Planned climb path: >=3m pressure-altitude gain within 3s AND mean climb >0.5m/s. No firmware changes.','',result['method'],'']
for x in maxima:lines.append(str(x))
lines+=['','Candidate: time-weighted mean fresh ground speed >=5mph over the last 3s AND net GPS displacement >=5m over those SAME 3s; require these combined window conditions for 1 additional elapsed second (normally two consecutive checks). This is an alternative OR path to climb and the future larger fallback bubble. A low individual reading changes the average rather than clearing a consecutive-speed timer.','',
 'The standalone speed average rejects hiking and launch-area motion, but does not reject the Bethany 01 reported-speed anomaly. Its reported speed is higher than some legitimate headwind launches, so a simple lower monotone speed boundary cannot separate those cases quickly. Position corroboration supplies the missing evidence.','',
 'The confirmed combined candidate rejects all classified ground windows for which it can be evaluated in the provided recordings. Lower speed thresholds are fitted to a small sample; running or faster walking remains a possible trigger. Freshness, position quality and velocity/position consistency deserve explicit handling. The 1Hz IGC positions are quantized, and idle positions before log start are absent.','',
 'Ideal straight motion at 5mph travels 6.71m in 3s, so it satisfies the two window limits; 6mph travels 8.05m. The 1s confirmation makes a cold sustained 5mph launch detect at around 4s. These are calculations, not strong-headwind flight validations. Turning tightly or hovering reduces net displacement and may leave detection to the separate climb / fallback paths. With a prefilled idle history, higher speeds can satisfy the mean and displacement before 3s of flight; a separate 3s launch-observation floor, if desired, must be specified rather than assumed.','',
 'Raw maximum hiking speed including prehistory is 4.74mph. The maximum 3s hiking mean is listed above. Including the first B fix reveals an initial Bethany-anomaly position excursion reaching 8.36m in a 3s window while reported speed is elevated. Speed>=5mph plus displacement>=5m alone therefore DOES trigger that anomaly. The extra confirmation rejects this isolated excursion; it does not reset merely because an individual speed sample drops below 5mph. A previous calculation that omitted the first fix understated this excursion.','']
for x in selected:
    lines +=[x['name'],'Ground files triggered: '+(', '.join(x['ground_files']) or 'none')]
    for f in x['flight_results']:lines.append(f"  {f['unit']}: {f['first_launch_PDT']} PDT; delay {f['launch_delay_s']}s; first recorded position delay {f['first_recorded_position_delay_s']}s")
    lines.append('')
lines+=['The recorded combined-rule delays for RedStrap and BlueGreen326 cannot show the full benefit of an idle position history: their first positions are already around/after takeoff. In a future implementation, sample the position window while idle. Racoon has continuous recorded preflight positions; Bethany has only pre-start speeds and begins logging about a second after its accepted launch reference.','',
 'The next choice is whether >=5mph and >=5m/3s is the intended fast headwind path, or whether to retain a more conservative speed floor such as 5.5mph. Keep a bounded history, invalidate discontinuities instead of inventing travel, and use actual net GPS displacement rather than a distance obtained from the same speed signal.']
(OUT/'forward-analysis.txt').write_text('\n'.join(lines),encoding='utf-8')
