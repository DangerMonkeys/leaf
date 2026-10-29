"""Follow-up: separate velocity estimates from actual pressure-altitude departure."""
from pathlib import Path
import json, math
import numpy as np

OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'features.json').read_text())

def windows(rows,seconds=6):
    for i,r in enumerate(rows):
        j=i
        while j>0 and r['t']-rows[j]['t']<seconds:j-=1
        w=rows[j:i+1];t=np.array([v['t'] for v in w]);duration=t[-1]-t[0]
        if duration<seconds or duration>=seconds+1.5:continue
        if np.max(np.diff(t),initial=0)>2:continue
        yield w,t,duration

result={'window_seconds':6,'interpretation':'Signed ascent and mirrored descent; pressure-altitude corroboration is censored before the first B record.',
        'ground':[],'synthetic_thermal':[]}
for d in D:
    pressure=[];climb=[];directional_min=[];pressure_hits=0;vario_hits=0
    for w,t,duration in windows(d['rows']):
        if not all(r['phase']=='ground' for r in w):continue
        a=np.array([r.get('alt',float('nan')) for r in w]);c=np.array([r['c'] for r in w])
        if np.isfinite(a).all():
            delta=float(a[-1]-a[0]);pressure.append(abs(delta))
            if abs(delta)>=6 and abs(delta)/duration>=1:pressure_hits+=1
        if np.isfinite(c).all():
            avg=float(np.trapezoid(c,t)/duration);climb.append(abs(avg))
            minimum=float(max(np.min(c),np.min(-c)));directional_min.append(minimum)
            if minimum>=1:vario_hits+=1
    if pressure or climb:
        result['ground'].append({'unit':d['unit'],'track':d['name'],'pressure_windows':len(pressure),'vario_windows':len(climb),
          'largest_absolute_pressure_altitude_change_m':max(pressure,default=None),
          'largest_absolute_mean_vario_mps':max(climb,default=None),
          'largest_same_direction_minimum_vario_mps':max(directional_min,default=None),
          'pressure_candidate_hits':pressure_hits,'six_seconds_vario_at_least_1_mps_hits':vario_hits})

# Idealized sensor readings: 20 seconds at rest, then six seconds of constant climb.
# This is a calculation, not validation against a recording of the friend's flight.
for rate in [1,1.5,2]:
    rr=[{'t':float(t),'alt':100+rate*min(max(t,0),6),'c':rate if 0<t<=6 else 0} for t in range(-20,13)]
    first=None
    for w,t,duration in windows(rr):
        delta=w[-1]['alt']-w[0]['alt'];mean_vario=float(np.trapezoid([r['c'] for r in w],t)/duration)
        if delta>=6 and delta/duration>=1 and mean_vario>=.8:
            first=w[-1]['t'];break
    result['synthetic_thermal'].append({'climb_mps':rate,'gain_over_six_seconds_m':6*rate,'idealized_trigger_seconds_after_climb_begins':first})

result['max_ground_six_second_pressure_change_m']=max(x['largest_absolute_pressure_altitude_change_m'] or 0 for x in result['ground'])
result['max_ground_six_second_absolute_mean_vario_mps']=max(x['largest_absolute_mean_vario_mps'] or 0 for x in result['ground'])
result['max_ground_six_second_same_direction_minimum_vario_mps']=max(x['largest_same_direction_minimum_vario_mps'] or 0 for x in result['ground'])
result['candidate_pressure_hits']=sum(x['pressure_candidate_hits'] for x in result['ground'])
(OUT/'thermal-analysis.json').write_text(json.dumps(result,indent=2))

summary='''Follow-up: thermal launch, fallback bubble, and continuous quiet

The pilot accepted the inferred takeoff times. The aim is to reject the observed
short ground-like episodes, with eventual bubble-triggered logging during a long
hike explicitly acceptable. The original 9 mph / 3 m/s score should not be the
only start path: 1 m/s climb at 1–2 mph will not trigger it.

Continuous quiet in the proposed stop analysis meant BOTH valid speed <1.5 m/s
and the highest-minus-lowest pressure altitude in the preceding 20 s <=3 m,
at every approximately one-second check for 20 elapsed seconds. A violation
resets the dwell. The analyzed candidate did not include an acceleration test.
The firmware currently does include a raw acceleration test, plus different
speed and altitude conditions. The 20 s altitude window and 20 s dwell are
separate, overlapping requirements, not an instruction always to wait 40 s.
In-flight hovering could satisfy low speed and stable altitude; these two flights
do not validate that case. Changing start sensitivity does not settle stop safety.

A velocity-only threshold >=1 m/s for six seconds cannot separate all requested
examples: BlueGreen326 06's pre-start history has a negative velocity estimate
near -2 m/s for several seconds. Sign reversal makes it look like thermal lift.
In the current firmware, climb comes from filtered IMU velocity, while alt()
is pressure-derived. Those are related signals, not identical measurements.

A candidate thermal path is >=6 m actual pressure-altitude rise within a 6 s
window, corroborated by positive mean vario (for example >=0.8 m/s). Observed
complete ground pressure-altitude windows top out at 5 m in either direction;
this candidate gives no pressure-only ground hits in the available windows.
The one-meter gap is narrow given the IGC's whole-meter altitude quantization.
Pre-start diagnostic history contains vario but not altitude, so the strongest
BlueGreen326 06 pre-start transient CANNOT be checked against this rule. No
claim of complete separation is justified until that history is captured.

Ideal inputs representing six seconds at 1, 1.5 or 2 m/s gain 6, 9 or 12 m.
With valid preceding idle history, the 6 m / 6 s + mean-vario candidate triggers
at 6, 4 or 3 s respectively. These are idealized calculations, not measured
performance on the friend's missing track or guarantees about sensor lag/noise.

Recommended architecture for further analysis: independent OR paths for
(1) sustained clear horizontal flight speed, (2) pressure-corroborated sustained
climb, and (3) a larger displacement bubble from an explicit idle anchor after
sensors stabilize. The anchor must not drift along with the pilot; rearming after
stop needs its own policy. A bubble of 100 m horizontally is only an illustration:
straight-line travel at 4–6 mph exits in 37–56 s, at 1–2 mph in 112–224 s. Turns
or hovering take longer or never exit. Vertical departure could be an independent
bubble condition, but its final size is not established by the available idle data.
Ground travel that exits this bubble is an accepted tradeoff, not a false-positive
constraint to optimize away. Avoid summing absolute pressure increments as a
replacement for net spatial departure, since noise also accumulates that sum.
'''
(OUT/'thermal-analysis.txt').write_text(summary,encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='ground'},indent=2))
print('Vario overlap:',[(x['unit'],x['track'],x['six_seconds_vario_at_least_1_mps_hits']) for x in result['ground'] if x['six_seconds_vario_at_least_1_mps_hits']])
