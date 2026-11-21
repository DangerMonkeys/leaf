from pathlib import Path
import sys, os, json, csv, math, statistics, html
OUT=Path(__file__).resolve().parent
os.environ['MPLCONFIGDIR']=str(OUT/'_mplconfig')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
tracks=json.loads((OUT/'new-tracks-parsed.json').read_text())
results=list(csv.DictReader((OUT/'new-tracks-results.csv').open()))
summary=json.loads((OUT/'new-tracks-summary.json').read_text())
bowen=tracks[0]
byms={int(b['ms']):b['utc']-25200 for b in bowen['b'] if 'ms' in b}
rr=[{k:float(v) for k,v in r.items()} for r in results if int(r['id'])==0]
ground=summary[0]['provisional_prelaunch_triggers']
print('Bowen pre-launch qualifying checks:',len(ground))
print('With pressure change >=4m:',sum(abs(r['alt_change_m'])>=4 for r in ground))
print('Largest corroborated spike:',max(ground,key=lambda r:abs(r['alt_change_m'])))
print('All-track checks:',len(results))
for d in tracks:
 bs=[b for b in d['b'] if 'ms' in b]
 print(d['pilot'],d['name'],'max diagnostic gap ms',max(b['ms']-a['ms'] for a,b in zip(bs,bs[1:])))
def hh(v,pos=None):
 v=round(v);return f'{v//3600:02}:{v//60%60:02}:{v%60:02}'
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(4,1,figsize=(13,11),layout='constrained')
fig.suptitle('Bowen: new detector replay reveals pre-launch vertical false starts\nOctober 25, 2026 · local time (PDT) · recorded altitude rounded to whole meters',fontsize=15)
pre=[b for b in bowen['b'] if b['utc']-25200<14*3600+3*60+9]
axes[0].plot([b['utc']-25200 for b in pre],[b['alt'] for b in pre],color='#2875a8',lw=.8,label='Recorded pressure altitude')
for r in ground:
 b=next(b for b in pre if int(b['ms'])==int(r['ms']))
 axes[0].scatter(b['utc']-25200,b['alt'],c='#cc3b3b',s=24,zorder=4)
axes[0].scatter([],[],c='#cc3b3b',s=24,label=f'Vertical trigger ({len(ground)} qualifying checks)')
axes[0].set(ylabel='Pressure altitude (m)',title='Ground-like period: GPS remains near launch while pressure altitude spikes')
axes[0].legend(loc='lower left',ncol=2)
speed={int(s[0]):s[1]/.44704 for s in bowen['samples']}
axes[1].plot([b['utc']-25200 for b in pre],[speed[int(b['ms'])] for b in pre],color='#177d62',lw=.8,label='Groundspeed')
axes[1].axhline(5,color='gray',ls='--',label='Forward mean-speed threshold (5 mph)')
axes[1].set(ylabel='Speed (mph)',ylim=(0,5.6),title='No forward or blended triggers in this pre-launch period')
axes[1].legend(loc='upper right',ncol=2)
zoom=[b for b in pre if 13*3600+47*60+25<=b['utc']-25200<=13*3600+47*60+43]
axes[2].plot([b['utc']-25200 for b in zoom],[b['alt'] for b in zoom],'o-',color='#2875a8',label='Pressure altitude')
ax2=axes[2].twinx()
ax2.plot([b['utc']-25200 for b in zoom],[b.get('VAR',0)/10 for b in zoom],'s-',color='#c37717',label='One-second vertical rate')
ax2.set_ylabel('Vertical rate (m/s)',color='#c37717')
for r in ground:
 if r['local'] in ('13:47:34','13:47:35','13:47:39'): axes[2].axvline(byms[int(r['ms'])],color='#cc3b3b',ls=':',lw=1)
axes[2].set(ylabel='Pressure altitude (m)',title='Example: a 7 m rise in a 3-second window passes the vertical test, then reverses')
launch=[b for b in bowen['b'] if 14*3600+3*60<=b['utc']-25200<=14*3600+3*60+35]
axes[3].plot([b['utc']-25200 for b in launch],[speed[int(b['ms'])] for b in launch],'o-',color='#177d62',label='Groundspeed (mph)')
axes[3].axvline(14*3600+3*60+15,color='#8847ad',ls='--',label='First blended trigger: 14:03:15')
axes[3].axvline(14*3600+3*60+17,color='#2875a8',ls='--',label='First forward trigger: 14:03:17')
axes[3].set(ylabel='Speed (mph)',title='Launch sequence: exact liftoff still needs pilot confirmation',xlabel='Local time (PDT)')
axes[3].legend(loc='upper left')
for ax in axes:
 ax.xaxis.set_major_formatter(FuncFormatter(hh));ax.grid(alpha=.2)
fig.savefig(OUT/'new-tracks-bowen.png',dpi=150)
fig.savefig(OUT/'new-tracks-bowen.svg')
report=f'''<!doctype html><meta charset="utf-8"><title>Three additional IGC tracks — detector replay</title>
<style>body{{font:17px system-ui;max-width:1050px;margin:35px auto;padding:0 20px;color:#18232c}}table{{border-collapse:collapse;width:100%}}td,th{{text-align:left;padding:12px;border-bottom:1px solid #ccd}}img{{width:100%}}code{{font-size:14px}}h1{{font-size:28px}}</style>
<h1>Three additional IGC tracks: vertical false starts exposed</h1>
<p>Replayed the committed <code>2431d929</code> C++ FlightAutoStart implementation, with no firmware modifications. All times October 25, 2026, PDT.</p>
<table><tr><th>Track</th><th>Result</th></tr>
<tr><td>Bowen · 01 (1)</td><td>First vertical trigger 13:46:15, well before the apparent launch sequence around 14:03:10–25. {len(ground)} qualifying checks in the provisional ground period; {sum(abs(r['alt_change_m'])>=4 for r in ground)} have at least 4 m pressure change. Forward and blended paths first trigger at 14:03:17 and 14:03:15, respectively.</td></tr>
<tr><td>Scott · 01</td><td>51-second ground-like record: no trigger in the evaluable data.</td></tr>
<tr><td>Scott · 02</td><td>Already moving at about 20 mph when recording begins at 14:03:37. Forward trigger 14:03:42, after 4.62 seconds of detector uptime. This is a recording-coverage limit, not a measured launch delay.</td></tr></table>
<p><b>Consequence:</b> the vertical-only 3 m / 3 s rule is not validated by this expanded dataset. Bowen's prelaunch pressure spikes and same-sign vertical-rate estimates satisfy it. The recorded 7–17 m spikes show this is more than a borderline whole-meter rounding effect. Their cause remains unknown.</p>
<img src="new-tracks-bowen.png" alt="Bowen pressure spikes, quiet prelaunch groundspeed, vertical trigger example and launch sequence">
<p><b>Coverage:</b> {len(results):,} updates, with identical decisions after reflecting altitude and vertical-rate signs. The runner continues assessing windows after its first trigger to expose later qualifying windows; counts are not distinct flight logs.</p>
<p><b>Replay limits:</b> all three headers report firmware v0.2.4-968, hardware v3.2.7, and contain 30 pre-start speed/climb samples (−29 s through zero). They do not contain pre-start pressure altitude or GPS positions. B-record pressure altitude is truncated to whole meters and VAR to 0.1 m/s; post-start speed comes from GL diagnostics. Full pre-start decisions and subsecond timing cannot be recovered. Barometer validity is approximated using the available CV diagnostic (which refers to the filtered climb signal); raw-altitude freshness and the new average-climb validity flag were not recorded. Window timing uses actual recorded uptime; displayed times use associated GPS B records. UI/summary inhibition and auto-stop lifecycle are not being replayed.</p>
<p>Exact liftoff in Bowen's launch sequence remains unconfirmed. No changes have been made to firmware or detector thresholds. Next step: investigate the pressure spikes and evaluate corroboration/persistence or robust pressure filtering before further tuning.</p>
<p>Inputs: <code>unknown hardware/2026-10-25-XLV-000-01 (1).igc</code>, <code>...-01.igc</code>, <code>...-02.igc</code>. Detailed windows: <a href="new-tracks-results.csv">CSV</a>. Summary: <a href="new-tracks-summary.json">JSON</a>.</p>'''
(OUT/'new-tracks-report.html').write_text(report,encoding='utf-8')
