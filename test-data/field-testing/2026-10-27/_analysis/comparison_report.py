from pathlib import Path
import json,csv,html
OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'parsed.json').read_text())
STOP=json.loads((OUT/'stop-threshold-sweep.json').read_text())
EARLIER=json.loads((OUT/'earlier-stop-sweep.json').read_text())
LAUNCH={0:46932,1:47942,3:47937,4:47942}
labels={0:'Raccoon',1:'Redstrap',2:'Blue ground',3:'Blue flight',4:'v3.2.8'}
variants={}
for name in ['baseline','forward7','distance11','shared7','forward5s','forward6_3s']:
 rows=list(csv.DictReader((OUT/f'variant-{name}.csv').open()))
 assert len(rows)==27719
 result=[]
 for d in D:
  lm={r['ms']:r['local'] for r in d['rows']}
  hits=[r for r in rows if int(r['id'])==d['id'] and int(r['triggered']) and (d['id']==2 or lm[int(r['ms'])]>=LAUNCH[d['id']]-20)]
  first=hits[0] if hits else None
  result.append(dict(id=d['id'],unit=d['unit'],ground_trigger=bool(hits) if d['id']==2 else None,delay_s=lm[int(first['ms'])]-LAUNCH[d['id']] if first and d['id']!=2 else None,mask=int(first['triggered']) if first else None))
 variants[name]=result
(OUT/'verified-start-options.json').write_text(json.dumps(variants,indent=2))
print(json.dumps(variants,indent=2))
# The Python parameter sweep must reproduce the independent C++ baseline.
baseline=list(csv.DictReader((OUT/'variant-baseline.csv').open()))
original=list(csv.DictReader((OUT/'replay-results.csv').open()))
assert baseline[:len(original)]==original
assert all(not next(x for x in variants[name] if x['id']==2)['ground_trigger'] for name in variants if name!='baseline')

def stop_config(n,mph,alt):return next(s for s in STOP if (s['checks'],s['speed_mph'],s['altitude_m'])==(n,mph,alt))
def delay(t):
 a=t.get('earliest_delay_s');b=t.get('latest_delay_s')
 if a is None:return 'Not established'
 return f'{a} s' if a==b else f'{a}–{b} s' if b is not None else f'From {a} s; later bound unresolved'
stop_rows=[]
for n,mph,alt in [(20,3,1),(15,3,1),(15,3,2),(10,3,2),(15,4,2),(10,4,2)]:
 c=stop_config(n,mph,alt)
 early=sum(t['possible_airborne_stop'] is not None for t in c['tracks'])
 earlier=[r for r in EARLIER if (r['checks'],r['speed_mph'],r['altitude_m'])==(n,mph,alt)]
 earlier_hits=sum(r['possible_airborne_stop'] is not None for r in earlier)
 stop_rows.append(f'<tr><td>{n} s</td><td>&lt;{mph} mph</td><td>&lt;{alt} m</td><td>{early} / 4</td><td>{earlier_hits} / {len(earlier)}' + (' (not swept)' if not earlier else '') + '</td>'+''.join(f'<td>{delay(t)}</td>' for t in c['tracks'])+'</tr>')
start_rows=[]
names={'baseline':'Current: 5 mph, 5 m, +1 s','forward7':'Forward only: 7 mph, 5 m, +1 s','distance11':'Forward only: 5 mph, 11 m, +1 s','forward5s':'Forward only: 5 mph, 5 m, +5 s','forward6_3s':'Forward only: 6 mph, 5 m, +3 s','shared7':'Raise shared speed scale to 7 mph (also changes blend)'}
for name in names:
 rr=variants[name];blue=next(t for t in rr if t['id']==2)
 start_rows.append('<tr><td>'+names[name]+'</td><td>'+('Triggers' if blue['ground_trigger'] else 'Rejected')+'</td>'+''.join('<td>'+str(next(t for t in rr if t['id']==i)['delay_s'])+' s</td>' for i in [0,1,3,4])+'</tr>')
report=f'''<!doctype html><meta charset="utf-8"><title>Threshold comparison with confirmed October 27 landings</title>
<style>body{{font:16px/1.55 system-ui;max-width:1300px;margin:35px auto;padding:0 24px;color:#17232e}}h1{{font-size:29px}}h2{{margin-top:32px;font-size:23px}}table{{border-collapse:collapse;width:100%;font-size:14px}}td,th{{padding:10px;text-align:left;border-bottom:1px solid #ccd;vertical-align:top}}th{{background:#edf2f5}}.note{{background:#fff3dc;padding:14px 18px;border-left:5px solid #b87b24}}code{{font-size:13px}}a{{color:#1b639a}}</style>
<h1>Faster auto-stop and rejection of Blue's ground auto-start</h1>
<p>Updated with pilot-confirmed October 27 landing times: <b>Raccoon 13:19:15; Redstrap, Blue and v3.2.8 13:29:37 PDT</b>. The previously reported launch-sequence trigger times were accepted by the pilot. No firmware changes have been made.</p>
<h2>Auto-stop</h2>
<p><b>A reasonable next candidate is 15 qualifying checks (approximately 15 seconds), speed below 3 mph, altitude strictly within 2 m of the retained anchor, and the existing ±0.2 g acceleration requirement.</b> The existing no-GPS speed bypass is preserved. The altitude threshold is departure from a retained anchor, not a 2 m full rolling range; opposite excursions can span nearly 4 m.</p>
<p>Both this candidate and a more aggressive 10-second version produce <b>no possible in-flight stops under the sampled-input reconstruction across all ten flight-containing recordings from October 25 and 27</b>. The earlier recordings contain repeated views of the same flights; these are not ten independent flight conditions. With speed kept below 3 mph, the longest airborne speed/acceleration-only quiet runs are eight checks in the October 27 set and at most eight in the earlier set. Thus 10 checks has relatively little margin against unrecorded smooth, slow flight; 15 has more margin.</p>
<p class="note">The current configuration's 21/23-second landing delays remain the <b>earliest compatible delays</b>, not observed auto-stops. Auto-stop was disabled. The missing post-start pressure fractions still prevent exact reconstruction. The table retains that uncertainty rather than treating each whole-meter B altitude as an exact sensor reading.</p>
<table><tr><th>Confirmation</th><th>Speed</th><th>Anchor altitude</th><th>October 27 airborne stops</th><th>Earlier airborne stops</th><th>Raccoon landing delay</th><th>Redstrap delay</th><th>Blue delay</th><th>v3.2.8 delay</th></tr>{''.join(stop_rows)}</table>
<p>Widening the altitude tolerance to 2 m materially reduces sensitivity to small pressure fluctuations. Increasing the speed threshold to 4 mph does not improve these October 27 landing results, but permits longer airborne quiet runs. Retain 3 mph initially. A 15-second counter with the present halfway-warning rule gives roughly eight seconds to cancel; a 10-second counter gives about five. If a full ten-second warning is required, warning timing needs separate design.</p>
<h2>Auto-start: how much tightening rejects Blue?</h2>
<p>Blue's actual forward trigger occurred around 13:04:50. The three-second mean at that instant was 6.45 mph, but the following rolling mean rose to <b>7.22 mph</b>. Therefore raising the threshold just above 6.45 mph does not reject the event. Later qualifying displacement also exceeds the initial 9.41 m and reaches <b>10.90 m</b>.</p>
<p>With the existing one-second confirmation, rejection begins around a <b>6.89–6.9 mph</b> speed threshold or a <b>10.09–10.1 m</b> displacement threshold in this replay. Use practical margins such as <b>7 mph</b> or <b>11 m</b> rather than tuning to those narrow boundaries. A 6.5 mph speed threshold and a 10 m displacement threshold each still trigger. Confirmation alone must exceed the roughly 4.26-second qualifying run; five additional seconds rejects it, whereas four still triggers. A balanced alternative is <b>6 mph plus three additional confirmation seconds</b>.</p>
<p><b>Keep the blended path's existing normalization separate</b> if changing only the standalone forward test. The current firmware shares the 5 mph / 5 m scale with the blended path; simply changing that constant also makes the blended path less sensitive. That costs v3.2.8 about 11 seconds in these flights.</p>
<table><tr><th>Option</th><th>Blue ground episode</th><th>Extra delay: Raccoon</th><th>Redstrap</th><th>Blue flight</th><th>v3.2.8</th></tr>{''.join(start_rows)}</table>
<p>All selected alternatives were verified by compiling analysis-only copies of the actual C++ detector and replaying <b>27,719 updates across the 29 October 25/27 recordings</b>. The actual firmware header and branch remain unchanged. Sign reflection gives identical decisions for every variant. The independently recompiled baseline exactly matches the saved 7,998 October 27 replay rows.</p>
<p>These results do not establish universal ground/flight separation. A level headwind launch at 5–6 mph would no longer qualify through a 7 mph standalone forward path; other paths help only when their vertical evidence is sufficient. Increasing displacement to 11 m within three seconds requires about 8.2 mph of straight-line motion, so it is a stronger low-speed restriction. A 6 mph / three-second confirmation alternative preserves sensitivity at 6 mph but takes about six seconds of ideal sustained motion in the forward-only case.</p>
<details><summary>Method and saved results</summary><p>Start sweeps use time-weighted windows from the actual C++ implementation. Precise pre-start diagnostics allow exact replay of Blue's original trigger; counterfactual later windows use rounded B pressure altitude and VAR. Post-start CV is an available proxy for the one-second-average validity flag. Stop sweeps retain the current counter/anchor algorithm, current acceleration gate, and no-GPS fallback; all possible integer-centimeter pressures consistent with each positive whole-meter B altitude are propagated through retained-anchor/count states. Stop states are absorbed to bound the first stop. Results assume the sampled B/GL/GA inputs represent each stop check and no warning is canceled. These bounds are model estimates, not device guarantees or actual stop timestamps.</p>
<p><a href="stop-threshold-sweep.json">Full stop sweep</a> · <a href="earlier-stop-sweep.json">Earlier stop recordings</a> · <a href="start-threshold-sweep.json">Full start sweep</a> · <a href="verified-start-options.json">C++-verified start options</a> · <a href="joint-start-options.json">Joint speed/confirmation sweep</a> · <a href="report.html">Initial October 27 report and plots</a></p></details>'''
(OUT/'threshold-comparison.html').write_text(report,encoding='utf-8')
initial=OUT/'report.html'
s=initial.read_text(encoding='utf-8')
if 'threshold-comparison.html' not in s:s=s.replace('<h1>', '<p><b>Updated analysis:</b> <a href="threshold-comparison.html">Threshold comparison using pilot-confirmed landing times</a>.</p><h1>',1)
initial.write_text(s,encoding='utf-8')
print('Saved threshold-comparison.html')
