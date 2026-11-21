from pathlib import Path
import os,sys,json,csv,html
OUT=Path(__file__).resolve().parent
os.environ['MPLCONFIGDIR']=str(OUT/'_mplconfig')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter,MaxNLocator
D=json.loads((OUT/'parsed.json').read_text())
S=json.loads((OUT/'summary.json').read_text())
B=json.loads((OUT/'stop-bounds.json').read_text())
R=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((OUT/'replay-results.csv').open())]
def hh(v,pos=None):
 t=round(v);return f'{t//3600:02}:{t//60%60:02}:{t%60:02}'
labels=['Raccoon · v3.2.3','Redstrap · v3.2.3','Blue · ground recording','Blue · flight recording','v3.2.8']
colors={'forward':'#2c6ca2','vertical':'#bb700c','blended':'#8e4998'}
plt.rcParams.update({'font.size':9,'axes.spines.top':False})
fig,axes=plt.subplots(4,2,figsize=(15,12),layout='constrained')
fig.suptitle('October 27: launch and landing review\nCurrent detector replay · local PDT · exact liftoff still needs pilot confirmation',fontsize=16)
for ix,i in enumerate([0,1,3,4]):
 d=D[i];s=S[i];launch=s['launch_band'];land=s['landing_band']
 for col,limits in enumerate([(launch[0]-20,launch[1]+20),(land[0]-20,land[1]+100)]):
  ax=axes[ix,col]
  rows=[r for r in d['rows'] if limits[0]<=r['local']<=limits[1]]
  ax.plot([r['local'] for r in rows],[r['s']/.44704 for r in rows],color='#237e62',lw=1.4,label='Speed')
  other=ax.twinx()
  other.plot([r['local'] for r in rows],[r['alt'] for r in rows],color='#5676ad',alpha=.7,lw=1,label='Pressure altitude')
  other.set_ylabel('Pressure altitude (m)',color='#5676ad')
  ax.set_ylabel('Speed (mph)',color='#237e62');ax.set_xlim(limits);ax.set_ylim(bottom=0)
  ax.xaxis.set_major_formatter(FuncFormatter(hh));ax.xaxis.set_major_locator(MaxNLocator(5));ax.grid(alpha=.2)
  ax.set_title(labels[i]+(' · launch sequence' if col==0 else ' · landing sequence'))
  if col==0:
   ax.axvspan(*launch,color='#aaa',alpha=.14,label='Unconfirmed liftoff interval')
   for path in ['vertical','blended','forward']:
    hit=s['paths'][path]
    if not hit:continue
    sec=next(r['local'] for r in d['rows'] if int(r['ms'])==int(hit['ms']))
    ax.axvline(sec,color=colors[path],ls='--',lw=1,label=path+' '+hh(sec))
   ax.legend(fontsize=7,loc='upper left',framealpha=.9)
  else:
   ax.axvspan(*land,color='#aaa',alpha=.2,label='Apparent touchdown interval')
   earliest=B[i]['from_launch']['earliest_possible']
   if earliest:ax.axvline(earliest['seconds'],color='#ab4242',ls='--',label='Earliest compatible stop '+earliest['local'])
   ax.legend(fontsize=7,loc='upper right',framealpha=.9)
fig.savefig(OUT/'flight-boundaries.png',dpi=140)
fig.savefig(OUT/'flight-boundaries.svg')

d=D[2];start=int(d['start']['T_MS']);rr=[r for r in R if int(r['id'])==2 and start-10000<=r['ms']<=start+12000]
fig,axes=plt.subplots(3,1,figsize=(12,9),sharex=True,layout='constrained')
fig.suptitle('Blue 01: confirmed ground auto-start through the forward path\nZero is the actual timer start (about 13:04:50 PDT)',fontsize=15)
raw=[r for r in d['rows'] if start-10000<=r['ms']<=start+12000]
axes[0].plot([(r['ms']-start)/1000 for r in raw],[r['s']/.44704 for r in raw],color='#619681',label='Individual speed')
axes[0].plot([(r['ms']-start)/1000 for r in rr],[r['mean_speed_mps']/.44704 for r in rr],color='#246a54',lw=2,label='3-second average speed')
axes[0].axhline(5,color='#888',ls='--',label='5 mph threshold');axes[0].set_ylabel('Speed (mph)');axes[0].legend(loc='upper left')
axes[1].plot([(r['ms']-start)/1000 for r in rr],[r['displacement_m'] for r in rr],color='#3c72a4',label='3-second GPS displacement')
axes[1].axhline(5,color='#888',ls='--',label='5 m threshold');axes[1].set_ylabel('Displacement (m)');axes[1].legend(loc='upper left')
axes[2].step([(r['ms']-start)/1000 for r in rr],[int(r['candidates'])&1 for r in rr],where='post',color='#9f711e',label='Forward candidate')
axes[2].step([(r['ms']-start)/1000 for r in rr],[int(r['triggered'])&1 for r in rr],where='post',color='#b64444',label='Confirmed trigger')
axes[2].set(yticks=[0,1],ylabel='Qualifies',xlabel='Seconds relative to actual timer start');axes[2].legend(loc='upper left')
for ax in axes:ax.axvline(0,color='#333',ls=':');ax.grid(alpha=.2)
fig.savefig(OUT/'blue-ground-start.png',dpi=150)
fig.savefig(OUT/'blue-ground-start.svg')

def event(x):return x['local'] if x else 'Not established before file ends'
start_rows=[];stop_rows=[];timer_rows=[]
flat=[]
for d,s,b in zip(D,S,B):
 hits=[(p,h) for p,h in s['paths'].items() if h]
 first=min(hits,key=lambda pair:pair[1]['ms']) if hits else None
 verdict='Confirmed ground false start; one episode' if d['id']==2 else 'Launch-sequence trigger; no earlier ground trigger in replay'
 start_rows.append(f'<tr><td>{html.escape(labels[d["id"]])}</td><td>{d["name"][-6:]}</td><td>{d["start"]["CAUSE"]}</td><td>{first[1]["local"] if first else "None"} ({first[0] if first else "—"})</td><td>{verdict}</td></tr>')
 if b['from_launch']:
  early=b['from_launch']['earliest_possible'];late=b['from_launch']['latest_across_reconstructions']
  stop_rows.append(f'<tr><td>{html.escape(labels[d["id"]])}</td><td>{hh(s["landing_band"][0])}–{hh(s["landing_band"][1])}</td><td>{event(early)}</td><td>{event(late)}</td><td>{s["stop_proxy"]["max_inflight_speed_accel_checks"]}</td></tr>')
 timer_rows.append(f'<tr><td>{html.escape(labels[d["id"]])}</td><td>{event(b["from_timer_start"]["earliest_possible"])}</td><td>{event(b["from_timer_start"]["latest_across_reconstructions"])}</td></tr>')
 flat.append(dict(unit=d['unit'],file=d['name'],recorded_start_cause=d['start']['CAUSE'],recorded_end_cause=d['end']['CAUSE'],replayed_first_path=first[0] if first else '',replayed_first_time=first[1]['local'] if first else '',ground_trigger_episodes=1 if d['id']==2 else 0,stop_earliest_after_landing=event(b['from_launch']['earliest_possible']) if b['from_launch'] else 'ground-only',stop_latest_after_landing=event(b['from_launch']['latest_across_reconstructions']) if b['from_launch'] else 'ground-only'))
with (OUT/'results.csv').open('w',newline='',encoding='utf-8') as f:
 w=csv.DictWriter(f,fieldnames=flat[0].keys());w.writeheader();w.writerows(flat)
report=f'''<!doctype html><html><head><meta charset="utf-8"><title>October 27 auto-start and auto-stop review</title>
<style>body{{font:16px/1.55 system-ui;max-width:1250px;margin:35px auto;padding:0 24px;color:#17232e}}h1{{font-size:30px}}h2{{font-size:23px;margin-top:35px}}table{{border-collapse:collapse;width:100%;font-size:14px}}td,th{{padding:10px;text-align:left;border-bottom:1px solid #cbd1d6;vertical-align:top}}th{{background:#edf2f5}}img{{width:100%;height:auto}}.finding{{padding:15px 20px;background:#fff3dd;border-left:5px solid #b87621}}code{{font-size:13px}}summary{{cursor:pointer}}a{{color:#1b639a}}</style></head><body>
<h1>October 27: one confirmed ground start; no modeled in-flight stops</h1>
<p>Reviewed <b>five October 27 IGC files from four units</b>. All report firmware v0.2.4-445 and the agreed start configuration. Four recordings were started manually; Blue 01 was started automatically. The other 54 IGCs in the folders have different header dates and were excluded. All displayed times are PDT on October 27, 2026; GPS/event alignment is approximate to about 1–2 seconds.</p>
<p class="finding"><b>Blue 01 is a confirmed ground false start.</b> The pilot confirmed the entire record was ground activity. Its actual START record says AUTO and its exact final STARTPATH is Q=01 T=01 (forward). Replaying the committed C++ detector reproduces that decision. This is one start episode, not five separate logs.</p>
<h2>Auto-start</h2>
<table><tr><th>Unit</th><th>File</th><th>Actual start</th><th>First relevant replay trigger</th><th>Interpretation</th></tr>{''.join(start_rows)}</table>
<p>Blue 01's triggering three-second window averaged <b>2.884 m/s (6.45 mph)</b>, with <b>9.41 m net GPS displacement</b>. The forward candidate had been active for approximately one additional second. Net pressure departure was −2.93 m while mean vertical rate was +0.21 m/s, so the vertical and blended paths did not cause this start.</p>
<p>The exact 30-sample pre-start histories now include pressure altitude to centimeters, GPS positions, and actual candidate/trigger masks. After allowing the replay to warm up, all <b>125 comparable pre-start path snapshots match</b> the recorded masks. During recording, pressure altitude and VAR are rounded, so later replay boundaries are approximate.</p>
<p>The independent paths help: Blue's flight record first qualifies vertically at 13:18:57 versus 13:19:03 forward; v3.2.8 first qualifies blended at 13:19:02 versus 13:19:13 forward. These compare detector paths, not delays after confirmed liftoff. Raccoon's apparent launch sequence is around 13:02:05–15; the other flight recordings show launch activity around 13:18:45–13:19:12. Exact liftoff needs pilot confirmation before assigning launch delays.</p>
<img src="blue-ground-start.png" alt="Blue ground start: speed, displacement and one-second forward confirmation">
<h2>Auto-stop: unchanged thresholds, hypothetical replay</h2>
<p>The pilot believes auto-stop was disabled on all units; no warnings were canceled. All actual END records are MANUAL or POWER_OFF. The following estimates therefore assume auto-stop was enabled, the required sensors were valid, and no warning was dismissed. The no-GPS fallback is preserved, as requested.</p>
<p>Current conditions: pressure altitude strictly within 1 m of its retained anchor; speed below 3 mph when usable GPS exists (otherwise speed is bypassed); valid acceleration within 0.2 g of 1 g; 20 qualifying scheduled checks. A failure resets the count and altitude anchor; the warning appears halfway through.</p>
<p><b>No in-flight stop evidence:</b> during the evaluated airborne portions, the longest runs passing speed and acceleration alone were 1, 1, 1, and 8 checks respectively. Thus none could reach the required 20 checks, even without testing altitude. This result applies to these recorded flights; it does not establish behavior in stationary hovering.</p>
<p><b>Exact landing-stop times cannot be recovered.</b> Post-start B records truncate pressure altitude to whole meters, while the test compares centimeters against a strict 1 m threshold. The table shows the earliest and latest first-stop times allowed by all possible centimeter values within those recorded whole meters, under the sampled-input model. A wide range means the recording cannot resolve timing; it is not a measured firmware delay.</p>
<table><tr><th>Unit</th><th>Apparent landing interval</th><th>Earliest compatible stop</th><th>Latest across pressure reconstructions</th><th>Longest airborne speed/IMU quiet run</th></tr>{''.join(stop_rows)}</table>
<p>For Blue 02, some compatible pressure sequences allow a stop around 13:30:00, while others do not stop before the manual close at 13:31:31. The records therefore cannot establish a precise stop time. Raccoon and the other units have substantially longer post-landing coverage.</p>
<h2>Long preflight manual recordings would also stop on the ground</h2>
<p>If auto-stop had been enabled from the actual timer starts, it could close manually started logs before takeoff. That is ordinary ground detection, not an in-flight false landing. The following are compatible first-stop bounds from the beginning of each file, independently of the landing estimates above.</p>
<table><tr><th>Unit / file</th><th>Earliest compatible stop from timer start</th><th>Latest across pressure reconstructions</th></tr>{''.join(timer_rows)}</table>
<p>In particular, Blue 01's unwanted ground log could stop around 13:05:17–13:06:32 under the sampled-input model instead of continuing to its actual manual close at 13:14:05. Auto-start could subsequently restart a log on qualifying launch motion.</p>
<img src="flight-boundaries.png" alt="Launch and landing sequences for four flight-containing records">
<h2>Conclusion</h2>
<p>The new paths respond to all four apparent launch sequences, but Blue 01 proves that the current forward threshold also accepts some entirely ground activity. There is no evidence here for changing auto-stop thresholds; the main stop-analysis limitation is the missing post-start fractional pressure altitude and actual stop counter/anchor diagnostics. No firmware changes were made.</p>
<details><summary>Method, limitations, and saved data</summary>
<p>Auto-start uses FlightAutoStart from commit 8d6cb8c5 in codex/flight-start-motion. The actual header is compiled and run in a read-only firmware mount. Signed climb and pressure altitude are also reflected; every decision and path mask remains identical. During the logged flight, the replay evaluates hypothetical idle/start conditions continuously; it does not simulate restarting the actual active log. Consecutive qualifying rows are not separate start events. Pre-start flags reproduce exact sensor validity; post-start CV is a proxy referring to the filtered climb signal, not the exact one-second-average validity used by auto-start. Pressure freshness and precise sampling order were not fully recorded. Gaps over two seconds reset replay history.</p>
<p>Auto-stop reconstruction retains the existing counter and no-GPS behavior. Pressure bounds enumerate all integer-centimeter values that could truncate to each B-record altitude. Possible anchor/count states are propagated; stop states are absorbed. These bounds are conditional on the recorded sampled inputs representing the actual stop checks, without user cancellation. They are not exact-device guarantees. Flight boundaries are inferred and remain unconfirmed. All originals are unmodified.</p>
<p><a href="results.csv">Summary CSV</a> · <a href="summary.json">Detailed start analysis</a> · <a href="stop-bounds.json">Pressure-quantization stop bounds</a> · <a href="replay-results.csv">All C++ replay windows</a> · <a href="inventory.json">All-file inventory</a></p></details>
</body></html>'''
(OUT/'report.html').write_text(report,encoding='utf-8')
print('Saved report.html, results.csv, flight-boundaries.png, blue-ground-start.png')
