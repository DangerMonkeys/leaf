from analyze import OUT, hh, number
import json, math, csv
import numpy as np

D=json.loads((OUT/'parsed.json').read_text())
def xy(lat,lon):
    return np.array([lon*math.pi/180*6371000*math.cos(lat*math.pi/180),lat*math.pi/180*6371000])

def make(d):
    g=d['gl']; base=number(d['start'],'T_MS')/1000
    b=d['b']; bt=np.array([x.get('ms',0)/1000 for x in b])
    # Map exact diagnostic uptime onto GPS UTC using median paired B/GL anchors.
    offset=float(np.median([x['utc']-x['ms']/1000 for x in b if 'ms' in x]))-25200
    rows=[]
    for x in d['pre']:
        flag=int(x['F'],16)
        rows.append({'t':base+number(x,'DT_MS')/1000,'s':number(x,'V')*.44704 if flag&1 else float('nan'), 'c':number(x,'C') if flag&2 else float('nan'), 'source':'pre'})
    for x in g:
        t=number(x,'T')/1000
        j=int(np.argmin(abs(bt-t)))
        bb=b[j] if abs(bt[j]-t)<.3 else None
        # STARTDATA is the exact pre-start input; B VAR is the rounded one-second
        # average used by auto-start. GL C is a DIFFERENT, filtered climb signal.
        rows.append({'t':t,'s':number(x,'S') if x['SV']=='1' else float('nan'),
          'c':bb['VAR']/10 if bb and 'VAR' in bb and x['CV']=='1' else float('nan'),
          'alt':bb['alt'] if bb else float('nan'), 'lat':bb['lat'] if bb else float('nan'),'lon':bb['lon'] if bb else float('nan'),
          'raw':number(next((a for a in d['ga'] if a['T']==x['T']),{}),'R'), 'source':'track'})
    # Discard duplicate same-check manual ending snapshots; preserve separate pre sample.
    rr=[]
    for r in rows:
        if rr and r['t']-rr[-1]['t']<.5:
            if rr[-1]['source']=='pre': continue
            else: continue
        rr.append(r)
    rows=rr
    for i,r in enumerate(rows):
        r['local']=r['t']+offset; r['rel']=r['t']-base
        for w in [3,5,10,20,30]:
            j=i
            while j>0 and r['t']-rows[j]['t']<w: j-=1
            window=rows[j:i+1]; dt=r['t']-rows[j]['t']; delta=np.diff([q['t'] for q in window])
            complete=dt>=w and dt<w+2 and (len(delta)==0 or np.max(delta)<=2)
            for key in ['s','c']:
                v=np.array([q[key] for q in window]); valid=np.isfinite(v).all()
                r[f'{key}{w}']=float(np.trapezoid(v,[q['t'] for q in window])/dt) if complete and valid else float('nan')
                if key=='s': r[f'smin{w}']=float(np.min(v)) if complete and valid else float('nan')
            r[f'z{w}']=abs(r.get('alt',float('nan'))-window[0].get('alt',float('nan'))) if complete else float('nan')
            av=np.array([q.get('alt',float('nan')) for q in window]); r[f'range{w}']=float(np.ptp(av)) if complete and np.isfinite(av).all() else float('nan')
            if complete and math.isfinite(r.get('lat',float('nan'))) and math.isfinite(window[0].get('lat',float('nan'))):
                a=window[0]; r[f'd{w}']=math.hypot((r['lon']-a['lon'])*111195*math.cos(r['lat']*math.pi/180),(r['lat']-a['lat'])*111195)
            else: r[f'd{w}']=float('nan')
    return rows

for d in D:
    d['rows']=make(d)
    flight=d['summary']['duration']>500
    if flight:
        d['category']='Flight-containing (inferred)'
        # Common launch and landing windows inferred from acceleration and altitude.
        d['launch']=48394 if d['unit']=='Bethany' else 50650
        d['land']=49299 if d['unit']=='Bethany' else 52775
    elif d['unit']=='Bethany': d['category']='Ground / GNSS anomaly (unconfirmed)'
    elif int(d['name'][-6:-4])<=14: d['category']='Hiking (user identified)'
    else: d['category']='Launch-site ground (inferred)'
    for r in d['rows']:
        r['phase']='ground'
        if flight:
            r['phase']='flight' if d['launch']+10<=r['local']<=d['land']-10 else ('ground' if r['local']<d['launch']-10 or r['local']>d['land']+20 else 'boundary')

ground=[r for d in D for r in d['rows'] if r['phase']=='ground']
print('Ground observations',len(ground))
for key in ['s','smin3','s3','s5','c3','c5','c10','z5','z10','z20','d5','d10','d20']:
    vals=[abs(r[key]) for r in ground if math.isfinite(r.get(key,float('nan')))]; print('ground maximum',key,round(max(vals),3))

rules={
 'speed >3m/s for 3s':lambda r:r['smin3']>3,
 'speed >3.5m/s for 3s':lambda r:r['smin3']>3.5,
 'speed >4m/s for 3s':lambda r:r['smin3']>4,
 'speed >4m/s for 5s':lambda r:r['smin5']>4,
 'speed >3m/s for 3s AND 5s displacement >12m':lambda r:r['smin3']>3 and r['d5']>12,
 'speed >2.5m/s for 3s AND 5s displacement >12m':lambda r:r['smin3']>2.5 and r['d5']>12,
 '5s mean speed >2.5m/s AND 5s displacement >12m':lambda r:r['s5']>2.5 and r['d5']>12,
 '10s displacement >25m':lambda r:r['d10']>25,
 '10s net vertical >6m':lambda r:r['z10']>6,
 '5s mean abs NET climb >0.8m/s':lambda r:abs(r['c5'])>.8,
 'speed>4/3s OR (speed>2.5/3s AND 5s displacement>12m)':lambda r:r['smin3']>4 or (r['smin3']>2.5 and r['d5']>12),
}
results=[]
for name,rule in rules.items():
    result={'rule':name,'ground_tracks':[],'launches':{}}
    for d in D:
        hit=[r for r in d['rows'] if r['phase']=='ground' and rule(r)]
        if hit: result['ground_tracks'].append(d['unit']+'/'+d['name'][-6:])
        if 'launch' in d:
            hits=[r for r in d['rows'] if r['local']>=d['launch']-10 and rule(r)]
            result['launches'][d['unit']]=[hh(hits[0]['local']),round(hits[0]['local']-d['launch'],1)] if hits else None
    results.append(result); print(result)

# Experimental stopping: compare conservative rolling horizontal/vertical windows,
# and reconstructed existing altitude-anchor/counter + raw-G at diagnostic times.
stops=[]
for d in D:
    if 'launch' not in d: continue
    rows=d['rows']; start=d['launch'];land=d['land']
    result={'unit':d['unit'],'launch_local':hh(start),'landing_reference':hh(land)}
    for speed in [1.34,1.5,2.0,2.5]:
      for dwell in [10,20,30]:
        quiet_since=None; first=None; false=[]
        for r in rows:
          if r['local']<start+30: continue
          quiet=r['s']<speed and r['range20']<=3
          if quiet:
            if quiet_since is None: quiet_since=r['t']
          else:quiet_since=None
          if quiet_since is not None and r['t']-quiet_since>=dwell:
            if r['local']<land-10: false.append(hh(r['local']))
            elif first is None: first=[hh(r['local']),round(r['local']-land,1)]
        result[f's<{speed},z20<=3,dwell={dwell}']={'first_after':first,'airborne_hits':len(false)}
    alt=0;count=0;first=None;false=0;longest=0
    for r in rows:
        if r['source']!='track':continue
        a=r.get('alt',float('nan'))
        if not math.isfinite(a):count=0;continue
        quiet=abs(a-alt)<1 and r['s']<3*.44704 and abs(r['raw']-1)<.2
        if quiet:count+=1;longest=max(longest,count)
        else:count=0;alt=a
        if count>=20:
            if start+30<r['local']<land-10:false+=1
            elif r['local']>=land-10 and first is None:first=[hh(r['local']),round(r['local']-land,1)]
    result['existing_proxy']={'first_after':first,'early_hits':false,'longest':longest}
    stops.append(result)
print('STOPS',json.dumps(stops,indent=2))

(OUT/'features.json').write_text(json.dumps(D,allow_nan=True))
(OUT/'rules.json').write_text(json.dumps(results,indent=2))
(OUT/'stops.json').write_text(json.dumps(stops,indent=2))
