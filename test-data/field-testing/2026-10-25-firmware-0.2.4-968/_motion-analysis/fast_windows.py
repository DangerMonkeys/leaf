"""Compare exact 3/4-second altitude and vario windows; never modify inputs."""
from pathlib import Path
import json, math
import numpy as np

OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'features.json').read_text())
observations=[]

def group(d,r):
    if d['category'].startswith('Hiking'):return 'hiking'
    if d['unit']=='Bethany' and 'launch' not in d:return 'unknown Bethany ground'
    if 'land' in d and r['local']>d['land']:return 'post-landing ground'
    return 'launch-area ground'

for seconds in [3,4]:
  for d in D:
    rr=d['rows']
    for i,r in enumerate(rr):
      if r['phase']!='ground':continue
      j=i
      while j>0 and r['t']-rr[j]['t']<seconds:j-=1
      w=rr[j:i+1];t=np.array([q['t'] for q in w]);duration=t[-1]-t[0]
      if duration<seconds or duration>=seconds+1.5 or np.max(np.diff(t),initial=0)>2:continue
      if not all(q['phase']=='ground' for q in w):continue
      start=t[-1]-seconds
      exact_t=np.r_[start,t[t>start]]
      a=np.array([q.get('alt',float('nan')) for q in w]);c=np.array([q['c'] for q in w])
      row={'seconds':seconds,'group':group(d,r),'unit':d['unit'],'track':d['name'],'relative_s':r['rel'],'local_s':r['local'],
           'altitude_change_m':None,'mean_vario_mps':None,'minimum_positive_vario_mps':None,'minimum_negative_vario_mps':None}
      if np.isfinite(a).all():row['altitude_change_m']=float(a[-1]-np.interp(start,t,a))
      if np.isfinite(c).all():
        v=np.interp(exact_t,t,c)
        row['mean_vario_mps']=float(np.trapezoid(v,exact_t)/seconds)
        row['minimum_positive_vario_mps']=float(np.min(v));row['minimum_negative_vario_mps']=float(np.min(-v))
      observations.append(row)

summary=[]
for seconds in [3,4]:
  for category in sorted({x['group'] for x in observations}):
    rows=[x for x in observations if x['seconds']==seconds and x['group']==category]
    a=[x for x in rows if x['altitude_change_m'] is not None];c=[x for x in rows if x['mean_vario_mps'] is not None]
    both=[x for x in a if x['mean_vario_mps'] is not None]
    largest_alt=max(a,key=lambda x:abs(x['altitude_change_m']),default=None)
    largest_vario=max(c,key=lambda x:abs(x['mean_vario_mps']),default=None)
    summary.append({'seconds':seconds,'group':category,'pressure_window_count':len(a),'vario_window_count':len(c),'joint_window_count':len(both),
      'max_abs_altitude_change_m':abs(largest_alt['altitude_change_m']) if largest_alt else None,
      'max_abs_mean_vario_mps':abs(largest_vario['mean_vario_mps']) if largest_vario else None,
      'max_sustained_same_direction_vario_mps':max((max(x['minimum_positive_vario_mps'],x['minimum_negative_vario_mps']) for x in c),default=None),
      'largest_altitude_event':largest_alt,'largest_vario_event':largest_vario})

candidates=[]
for seconds in [3,4]:
  for gain in [3,4]:
    for corroboration in [None,.3,.5,.7]:
      hits=[]
      for x in observations:
        if x['seconds']!=seconds or x['altitude_change_m'] is None:continue
        for sign in [1,-1]:
          if sign*x['altitude_change_m']<gain:continue
          if corroboration is not None and (x['mean_vario_mps'] is None or sign*x['mean_vario_mps']<corroboration):continue
          hits.append({'unit':x['unit'],'track':x['track'],'group':x['group'],'local_s':x['local_s'],'mirror':sign==-1})
      candidates.append({'seconds':seconds,'gain_m':gain,'same_direction_mean_vario_min_mps':corroboration,'trigger_windows':len(hits),
                         'tracks':sorted({x['unit']+'/'+x['track'][-6:] for x in hits}),'hits':hits})

result={'method':'Exact window endpoints interpolated between adjacent 1 Hz readings using diagnostic uptime; mean vario is signed trapezoidal average, never mean absolute value. Gaps >2s rejected. Ground originals and vertical reflections evaluated. Missing pre-start pressure altitude remains unobservable.',
        'summary':summary,'candidates':candidates}
(OUT/'fast-window-analysis.json').write_text(json.dumps(result,indent=2))
for x in summary:print({k:v for k,v in x.items() if not k.endswith('_event')})
for x in candidates:print({k:v for k,v in x.items() if k!='hits'})

report='Three- and four-second launch windows\n\n'+result['method']+'\n\n'
report+='Complete hiking pressure-altitude windows peak at 2 m net change in BOTH 3 s and 4 s windows. The same holds after vertical reflection. Thus >=3 m in 3 s and >=4 m in 4 s both separate the available hiking pressure histories; >=4 m has more margin against whole-meter IGC quantization. Neither conclusion covers the unrecorded pre-start pressure histories.\n\n'
report+='A bare altitude endpoint test is vulnerable to a launch-area pressure spike: Racoon falls 639 ->634 m for one sample then returns to 639 m while the vario remains near zero. Max ground change is 5 m over either window. Requiring same-direction mean vario >=0.5 m/s as well as >=3 m or >=4 m altitude change rejects all available jointly valid ground windows for both durations, including mirrored ground. This is a fitted candidate, not independent validation.\n\n'
report+='Climb estimate alone still overlaps: the BlueGreen326 06 pre-start trace has mean magnitude 2.133 m/s over 3 s and 2.091 m/s over 4 s. The largest minimum magnitude sustained in one direction is 1.92 m/s over 3 s and 1.85 m/s over 4 s. To reject all these traces using mean-vario alone requires >2.133 m/s or >2.091 m/s respectively; a minimum-at-every-sample rule requires >1.92 or >1.85 m/s respectively. Such cutoffs miss much of the requested 1–2 m/s thermal-launch range.\n\n'
report+='Ideal altitude requirements: >=3 m within 3 s corresponds to >=1 m/s average over the full window; >=4 m within 4 s also corresponds to >=1 m/s. With stable idle history and a matching positive vario signal, 1 m/s flight reaches these at 3 or 4 s, 1.5 m/s at 2 or 2.67 s, and 2 m/s at 1.5 or 2 s. Actual once-per-second decisions round up to the next check; any explicit minimum-observation rule may impose a longer floor. These are ideal calculations, not observed sensor-response times.\n\n'
report+='Recommendation for the next evaluation: compare a 3 m / 3 s path and a 4 m / 4 s path, both corroborated by positive mean vario >=0.5 m/s. Prefer the latter if one second of extra delay is acceptable; keep the stronger horizontal-speed path and slower departure bubble independent. Capture idle pressure altitude at centimeter resolution alongside vario to close the key prehistory and quantization gaps. No firmware changes.\n'
(OUT/'fast-window-analysis.txt').write_text(report,encoding='utf-8')
