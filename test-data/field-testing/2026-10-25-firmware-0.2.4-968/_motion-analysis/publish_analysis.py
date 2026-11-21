from pathlib import Path
import json, math, csv, html
OUT=Path(__file__).resolve().parent
D=json.loads((OUT/'features.json').read_text()); rules=json.loads((OUT/'rules.json').read_text()); sweep=json.loads((OUT/'sweep.json').read_text());stops=json.loads((OUT/'stops.json').read_text())
def hh(t):
    t=int(t);return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'
def clean(v):
    if isinstance(v,float):return round(v,3) if math.isfinite(v) else None
    return v
charts=[]
for d in D:
    rows=[]
    for i,r in enumerate(d['rows']):
        # Keep all ground, launch/landing boundaries, and 1 Hz near either event.
        near='launch' in d and (abs(r['local']-d['launch'])<90 or abs(r['local']-d['land'])<120)
        if r['phase']=='flight' and not near and i%3:continue
        rows.append([clean(r.get(k)) for k in ['local','s','c','alt','d5','c5','smin3']]+[{'ground':0,'flight':1,'boundary':2}[r['phase']]])
    charts.append({'unit':d['unit'],'hardware':d['hardware'],'file':d['name'],'path':d['path'],'category':d['category'],'launch':d.get('launch'),'land':d.get('land'),'cause':d['start'].get('CAUSE'),'end':d['end'].get('CAUSE'),'rows':rows})
fragment=(OUT/'review.html').read_text(encoding='utf-8').replace('/*DATA*/[]',json.dumps(charts,separators=(',',':'),allow_nan=False))
viz=OUT/'motion-fragment.html'
viz.write_text(fragment,encoding='utf-8')
style=':root{--foreground:#202124;--border:#d5d7dc;--popover:#fff;--popover-foreground:#202124;--viz-series-1:#0f766e;--viz-series-2:#7c3aed;--viz-series-3:#b45309;--viz-series-4:#be123c}body{font:14px system-ui;margin:28px;max-width:1100px}select{font:inherit;padding:8px;max-width:100%}.viz-controls{display:flex;gap:18px;flex-wrap:wrap;margin:20px 0}.form-label{display:grid;gap:5px}h2{font-size:20px;font-weight:500}.text-small{font-size:12px}'
(OUT/'motion-review.html').write_text('<!doctype html><html><meta charset="utf-8"><title>October 25 motion analysis</title><style>'+style+'</style>'+fragment+'</html>',encoding='utf-8')

inventory=[]
for d in D:
    r=d['summary'].copy();r['category']=d['category'];r['inferred_takeoff_PDT']=hh(d['launch']) if 'launch' in d else '';r['inferred_touchdown_PDT']=hh(d['land']) if 'land' in d else '';inventory.append(r)
with (OUT/'categorized-tracks.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=inventory[0]);w.writeheader();w.writerows(inventory)

def table(headers,rows):
    return '<table><thead><tr>'+''.join('<th>'+html.escape(str(x))+'</th>' for x in headers)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+html.escape(str(x))+'</td>' for x in row)+'</tr>' for row in rows)+'</tbody></table>'
track_table=table(['Unit / HW','Track','PDT start–end','Category','Start / end','Altitude m','Max m/s'],[[d['unit']+' / '+d['hardware'].replace('hardware-',''),d['name'][-6:],d['summary']['local_start']+'–'+d['summary']['local_end'],d['category'],d['summary']['cause']+' / '+d['summary']['end'],str(d['summary']['alt_start'])+' → '+str(d['summary']['alt_end']),d['summary']['max_speed']] for d in D])
selected=[x for x in sweep if x['speed_mph'] in [8,9,10] and x['climb_mps'] in [2,3,None] and x['memory_s']==2.4]
sweep_table=table(['Speed scale mph','Climb scale m/s','Ground files hit, either sign','Worst ground score','Launch delay s: Racoon / Red / Blue / Bethany'],[[x['speed_mph'],x['climb_mps'] or 'disabled',', '.join(x['ground_tracks_either_direction']) or 'none',x['ground_max_score'],' / '.join(str(x['delay_s'][u]) for u in ['Racoon','RedStrap','BlueGreen326','Bethany'])] for x in selected])
rule_table=table(['Candidate','Ground files hit','Predicted start PDT (delay s)'],[[x['rule'],', '.join(x['ground_tracks']) or 'none','; '.join(u+': '+str(v) for u,v in x['launches'].items())] for x in rules])
stop_table=table(['Unit','Inferred touchdown PDT','Candidate stop: speed <1.5 m/s; range20 ≤3 m; 20s dwell','Existing rule proxy','Recorded manual end'],[[x['unit'],x['landing_reference'],str(x['s<1.5,z20<=3,dwell=20']['first_after'] or 'not enough post-landing recording'),str(x['existing_proxy']['first_after'] or 'not enough post-landing recording'),next(d['summary']['local_end'] for d in D if d['unit']==x['unit'] and 'launch' in d)] for x in stops])
content='''<h1>October 25, 2026: flight auto-start and auto-stop analysis</h1>
<p><b>Finding:</b> There is a useful separation in these recordings. Sustained horizontal speed provides the clearest simple boundary. Climb currently contributes far too much: ordinary hiking and launch-site barometric transients can trigger logging. An absolute displacement bubble anchored at power-on will eventually be crossed by hiking; a short rolling window is a better comparison.</p>
<p><b>Scope:</b> 263 IGC files scanned; 21 dated October 25 selected. Four units: Racoon and RedStrap (3.2.3), BlueGreen326 (3.2.6), Bethany (3.2.7). The hardware-3.2.5 directory contains no supplied tracks. 242 older files excluded. Original recordings and firmware were not changed. Separate output directory avoids the other chat’s _analysis files.</p>
<h2>Classification</h2>
<p>14 BlueGreen326 hiking files (01–14): 9 AUTO, 5 MANUAL; two launch-site ground files (15–16), both AUTO; one Blue flight (17). Racoon contains roughly 10 minutes of stationary launch-site activity before flight. RedStrap is the same afternoon flight. Bethany 02 is a separate earlier flight. Bethany 01 is provisionally ground / GNSS anomaly, pending pilot confirmation. Thus 17 files contain no apparent flight, and four contain flight, representing only two distinct flight events—not four independent launches.</p>
<p>Bethany 01, 08:40:39–08:41:30 PDT: barometric altitude stays at −42 m; the entire position cloud spans roughly 5.6 × 13.2 m, despite reported speed briefly reaching 3.802 m/s (8.5 mph). Its physical circumstances are unknown; retain as a conservative negative example until confirmed.</p>
<p>Provisional takeoff references: Bethany 13:26:34 PDT; afternoon flight 14:04:10 PDT. Provisional touchdown: Bethany 13:41:39; afternoon 14:39:35. These are inferred from acceleration, path and altitude, with several seconds of uncertainty; a pilot should mark exact lift-off and touchdown in the review charts.</p>
'''+track_table+'''
<h2>Why today’s definition false-starts</h2>
<p>The current score exponentially approaches min(speed_mph / 4.2 + positive_climb_mps / 0.7, cap), with 2.4 s memory and a minimum 3 s observation period. At 2.5 mph plus 0.4 m/s climb, its steady motion input is 1.17, enough to start. BlueGreen326 01 triggered at 3.418 mph and +0.35 m/s; 15 triggered at 0.829 mph and +0.96 m/s while remaining at the launch site. Racoon also started early. The relevant source is src/vario/logging/flight_auto_start.h.</p>
<p>Simply making descent count as positive climb is unsuitable. The hiking prehistory in BlueGreen326 06 contains a roughly −2 m/s multi-second vertical signal; its sign-inverted counterpart crosses several otherwise promising climb thresholds. Short-term integrated vario and actual barometric altitude change are not interchangeable in these traces.</p>
<h2>Start candidates</h2>
<p><b>Simple candidate for further evaluation:</b> valid ground speed &gt;4 m/s (8.95 mph) continuously for at least 3 s, resetting on invalid inputs or a &gt;2 s update gap. It rejects every labeled ground sample in this dataset, and vertical sign inversion leaves its decisions unchanged. Predicted starts are 14:04:15 Racoon/RedStrap, 14:04:16 BlueGreen326, and 13:26:46 Bethany. Approximate delays relative to the inferred lift-off references: 5–7 s on the afternoon event, 12 s for Bethany.</p>
<p>A 3.5 m/s sustained threshold also separates this data, but only narrowly clears the anomalous Bethany speed burst. 4 m/s has more margin. This speed-only candidate does not cover launches with very low groundspeed in strong headwind or missing GPS.</p>
<p><b>Minimal adjustment to the current score:</b> retain 2.4 s memory and 3 s observation, raise speed scale to 9 mph and climb scale to 3 m/s. It gives no ground triggers in either vertical direction, with worst modeled ground score 0.918 against a trigger of 1. Predicted delay is about 3–6 s on the afternoon event and 11 s on Bethany. 8 mph / 3 m/s is slightly faster but reaches 0.958 on ground data, leaving little margin. Reducing climb scale to 2 m/s fails on the reversed BlueGreen326 06 prehistory even with a 10 mph speed scale. These are fitted candidates, not validated universal thresholds; 3 m/s also weakens the GPS-free launch path substantially.</p>
<p>The score sweep tested 126 combinations: 6,7,8,9,10,12 mph × 1,1.5,2,2.5,3,4 m/s or no climb contribution × 2.4,4,6 s memory. 51 combinations rejected observed ground activity in both vertical directions and detected all four flight recordings. The tables show selected tradeoffs, not a claim that the best fit will generalize.</p>
'''+sweep_table+rule_table+'''
<h2>Bubble and multidimensional alternatives</h2>
<p>A bubble from power-on or an accumulated absolute path length will eventually fire on a hike. Blue hiking recordings alone move 18–123 m each, and the ascent spans hundreds of vertical meters. Full power-on trajectories are absent, so exact startup-bubble crossings cannot be replayed. Summing absolute altitude increments also accumulates barometric noise and loses the distinction between random motion and sustained departure.</p>
<p>A rolling 5 s displacement plus speed test is more useful. Mean speed &gt;2.5 m/s and horizontal displacement &gt;12 m in a complete 5 s window rejects observed ground activity and detects Bethany around 13:26:42 (8 s after the provisional lift-off), faster than the simple sustained-speed candidate. Across labeled ground data the largest 5 s displacement is 10.98 m, so 12 m has limited margin. The Blue recording does not contain pre-start positions, delaying any position-window test until five seconds after its first fix in the file; its corresponding firmware delay could be shorter with idle fix history. Exact equivalence cannot be established from these files.</p>
<p>Observed labeled-ground maxima: instantaneous speed 3.802 m/s; minimum speed maintained over a complete ≥3 s window 3.354 m/s; 5 s horizontal displacement 10.98 m; 10 s displacement 17.52 m; absolute 10 s net barometric change 6 m. Hiking-only instantaneous speed including prehistory stays below the launch speeds, but vigorous descent or running is not represented. Mirrored hiking is a sign-inverted counterfactual, not a recorded downhill experiment.</p>
<p>The charts show speed versus mean vertical speed and speed versus displacement, with mirrored ground vertical samples. Full flight interiors are reduced for display only; threshold evaluation uses every retained one-second observation. Different time-window definitions have different units and must not be interchanged with instantaneous thresholds.</p>
<h2>Auto-stop</h2>
<p>The current code needs 20 consecutive quiet checks: speed &lt;3 mph, barometric altitude within 1 m of a resettable anchor, raw acceleration within 0.2 g of 1 g. It can run without usable GPS; stale speed with a usable fix blocks quiet. Barometric readiness and valid IMU are required. The alert appears halfway through. These details are in src/vario/logging/log.cpp and log.h.</p>
<p>A candidate using valid speed &lt;1.5 m/s, altitude range ≤3 m over the last 20 s, and 20 elapsed seconds of continuous quiet has zero airborne stop detections in the recorded flights. On the afternoon flight it stops about 35–36 s after inferred touchdown, around 14:40:10–11. This includes the observed landing run / ground movement; there is no 20 s stillness at touchdown itself. Bethany’s recording ends around 14 s after inferred touchdown, so that case is censored and cannot validate a 20 s rule.</p>
<p>This candidate currently ignores acceleration; it is a diagnostic comparison, not a final safety proposal. Test rolling acceleration variation as corroboration rather than relying on one raw sample that can be disturbed by handling. Strong-wind hovering, top-landings and walking after landing need additional examples. Low ground speed plus near-zero net vertical speed can also happen in flight, so a relaxed stop rule cannot be declared safe from these two events.</p>
'''+stop_table+'''
<h2>Methods and limits</h2>
<ul><li>IGC B positions and pressure altitude parsed at their declared fields. I-record extensions identify GSP in km/h and VAR in tenths of m/s. GL diagnostic S is higher-precision ground speed in m/s. STARTDATA has actual auto-start speed in mph and one-second mean climb in m/s. GL diagnostic C is a different filtered climb signal; it is not used as the auto-start input.</li>
<li>Time mapped from diagnostic uptime to GPS UTC using the median B/GL time offset, then subtracting seven hours for PDT. Rounding / scheduler alignment limits subsecond claims; displayed delays are approximate and lift-off references are unconfirmed.</li>
<li>Post-start auto-start counterfactuals use rounded B VAR, conservatively masking when GL climb validity is false. One-second average validity is not directly logged post-start. Before start, STARTDATA is exact to its logged precision. The score replays start cold at the first available sample and evaluate hypothetical continuous detection, without replaying UI summary inhibition or enable settings. There are only up to 30 s of prehistory and no full idle history.</li>
<li>First pre-start sample establishes time; gaps &gt;2 s reset accumulation. Rolling windows include the first available sample at least the requested duration before the current sample, require no &gt;2 s update gap, and use actual timestamps with time-weighted means. Altitude uses net change or full range as named; position uses geodesic local displacement, never summed GPS jitter.</li>
<li>Ground classification includes all of the 17 nonflight files and inferred pre/postflight ground in the four flight files. To avoid training on uncertain transitions, exclude 10 s before lift-off through 10 s after lift-off, and 10 s before through 20 s after touchdown. Evaluate reported flight detections separately against the provisional references. Censored recording starts limit position-window comparisons.</li>
<li>Downhill checks negate vertical rate and altitude departures while preserving horizontal motion. Signed score replay is run twice; all speed/displacement/absolute-altitude rules are invariant to this reflection. This does not synthesize different downhill gait, GPS noise or sensor behavior.</li>
<li>Auto-stop’s current-rule reconstruction is only a proxy: B altitude is quantized to whole meters, the code uses centimeters, diagnostic samples occur after stop checking, barometric readiness and GPS freshness are only partly observable, and enable / alert intervention history is not recorded. All four flight-containing files have MANUAL end markers. This is not evidence that auto-stop was enabled or broken.</li>
<li>No complete raw IMU stream, no actual strong-headwind launch, no sustained running / vehicle trip, no independent downhill hike, and only two real flight events. A car or running pilot can cross speed / displacement boundaries. Independent labeled recordings are needed before selecting final firmware constants.</li></ul>
<h2>Next review</h2>
<p>Confirm whether Bethany 01 was indoors, carried, or in a vehicle; mark true takeoff / touchdown on both events; then compare a speed-led threshold with the 9 mph / 3 m/s score and a short rolling position corroboration. Keep a bounded pre-start track buffer if preserving the actual launch point matters: trigger promptly on evidence, then retain earlier fixes / candidate time instead of weakening detection merely to avoid losing the first seconds.</p>
<p>Reproduce locally by running analyze.py, features.py, sweep.py, then publish_analysis.py with a Python installation containing numpy. Outputs: categorized-tracks.csv, rules.json, sweep.json, stops.json, inventory.json (input hashes), parsed.json, features.json, and motion-review.html.</p>
'''
if (OUT/'thermal-analysis.txt').exists():
    update='''<h2>Follow-up: accepted takeoff times and revised start criteria</h2>
    <p>The pilot confirmed the inferred takeoff times and clarified that eventual
    logging during a long hike is acceptable. A startup displacement bubble is
    therefore an intentional fallback, rather than a behavior to reject. The
    original 9 mph / 3 m/s score is insufficient as the sole launch path for
    six seconds at 1–2 m/s climb and 1–2 mph groundspeed.</p>
    <p>A separate thermal path based on at least 6 m pressure-altitude gain within
    about 6 seconds, corroborated by positive vario, fits the available complete
    ground altitude windows (largest absolute change 5 m). However, the strongest
    hiking pre-start vario transient has no recorded altitude history. This path
    is promising but cannot yet be claimed to reject all short ground episodes.
    See <a href="thermal-analysis.txt">the follow-up analysis and precise quiet
    definition</a>. The initial assessment below remains the comparison under
    the original stricter hiking-rejection objective.</p>'''
    content=content.replace('</h1>','</h1>'+update,1)
css='body{font:16px/1.55 system-ui;max-width:1200px;margin:40px auto;padding:0 24px;color:#202124}h1,h2{line-height:1.2}h2{margin-top:36px}table{border-collapse:collapse;width:100%;font-size:13px}td,th{padding:8px;text-align:left;border-bottom:1px solid #ddd;vertical-align:top}th{background:#f3f4f6}p{max-width:1000px}'
(OUT/'report.html').write_text('<!doctype html><html><meta charset="utf-8"><title>Flight motion analysis — October 25</title><style>'+css+'</style>'+content+'</html>',encoding='utf-8')
print('Visualization bytes',viz.stat().st_size,'tracks',len(charts),'points',sum(len(t['rows']) for t in charts))
print('Report and categorized inventory saved to',OUT)
