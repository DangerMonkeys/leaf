"""Fresh, read-only October 25 replay of the 10/15-check, 2 m stop proposals."""
from pathlib import Path
import ast,json,hashlib,re,html
OUT=Path(__file__).resolve().parent
ROOT=OUT.parent
# Import only definitions: neither module's earlier batch analysis is executed.
for source,names in [(OUT/'analyze.py',('fields','number','parse','hh')),
 (ROOT.parent/'2026-10-27/_analysis/threshold_sweep.py',('stop_bounds',))]:
 tree=ast.parse(source.read_text(encoding='utf-8'))
 exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),'analysis_helpers','exec'))
manifest=[]
for d in json.loads((OUT/'features.json').read_text()):
 if 'launch' in d:manifest.append(dict(path=d['path'],label=d['unit'],launch=d['launch'],land=d['land']))
for d in json.loads((OUT/'new-tracks-parsed.json').read_text()):
 if len(d['b'])>100:
  manifest.append(dict(path=d['path'],label='Bowen' if d['id']==0 else 'Scott',launch=14*3600+3*60+(10 if d['id']==0 else 15),land=14*3600+50*60+(15 if d['id']==0 else 5)))
results=[]
for meta in manifest:
 p=ROOT/meta['path'];d=parse(p)
 assert d['date']=='251026'
 gs={int(g['T']):g for g in d['gl']};ga={int(g['T']):g for g in d['ga']}
 rows=[]
 for b in d['b']:
  if 'ms' not in b:continue
  ms=int(b['ms']);g=gs.get(ms);a=ga.get(ms,{})
  if not g:continue
  local=b['utc']-25200
  if local<meta['launch']:continue
  rows.append(dict(ms=ms,local=local,alt=b['alt'],flags=int(g['SV']=='1')|4|(8*int(b['valid'])),s=float(g['S']),raw=float(a.get('R','nan')),raw_valid=a.get('RV')=='1'))
 candidates={}
 for n in [10,15]:
  r=stop_bounds(rows,n,3,2,land=meta['land'])
  assert r['possible_airborne_stop'] is None
  candidates[str(n)]=r
 results.append(dict(**meta,sha256=d['sha256'],file_end_local=hh(d['b'][-1]['utc']-25200),recorded_end_cause=d['end'].get('CAUSE'),sample_count=len(rows),candidates=candidates))
 assert hashlib.sha256(p.read_bytes()).hexdigest()==d['sha256']
for r in results:
 print(r['label'],r['path'],'end',r['file_end_local'])
 for n,c in r['candidates'].items():print(n,c)
(OUT/'auto-stop-proposals.json').write_text(json.dumps(results,indent=2))
table=[]
for r in results:
 cells=[]
 for n in ['10','15']:
  c=r['candidates'][n]
  if c['earliest'] is None:label='Not reached before file ended'
  elif c['latest'] and c['latest']['ms']==c['earliest']['ms']:label=c['earliest']['local']
  else:label=c['earliest']['local']+' to '+(c['latest']['local'] if c['latest'] else 'unresolved')
  cells.append('<td>'+label+'</td>')
 table.append('<tr><td>'+html.escape(r['label'])+'</td>'+''.join(cells)+'<td>None</td></tr>')
report=f'''<!doctype html><meta charset="utf-8"><title>October 25 auto-stop proposal replay</title>
<style>body{{font:17px/1.55 system-ui;max-width:1000px;margin:35px auto;padding:0 22px;color:#17232e}}table{{border-collapse:collapse;width:100%}}td,th{{padding:12px;text-align:left;border-bottom:1px solid #ccd}}th{{background:#edf2f5}}code{{font-size:14px}}</style>
<h1>October 25: both auto-stop proposals pass the recorded flights</h1>
<p>Fresh replay from the six original flight-containing IGC files, including Bowen and Scott in <code>unknown hardware</code>. Ground-only records are excluded from the airborne-landing assessment. No original files or firmware were changed.</p>
<p>Both candidates retain speed below <b>3 mph</b> when GPS is usable (speed bypassed without GPS), valid acceleration within <b>0.2 g of 1 g</b>, and altitude strictly within <b>2 m of the retained anchor</b>. They differ only in requiring <b>10 or 15 consecutive qualifying checks</b>, approximately seconds at the normal one-Hz check rate. This is the existing anchor/counter algorithm, not a new full-range altitude window.</p>
<table><tr><th>Recording</th><th>10-second proposal</th><th>15-second proposal</th><th>Possible in-flight stops</th></tr>{''.join(table)}</table>
<p>All times are October 25 PDT. Bethany's file ends at {next(r['file_end_local'] for r in results if r['label']=='Bethany')}; neither proposal reaches confirmation within its recorded post-landing coverage. This is not an in-flight false stop.</p>
<p>Bowen has the longest recorded airborne run passing speed and acceleration alone: <b>8 consecutive checks</b>. Pressure stability still prevents a stop. A ten-check confirmation leaves relatively little margin against longer unrecorded calm, slow flight; fifteen is the more conservative of the two proposals. These six recordings include repeated views of the same flights and are not six independent flight conditions.</p>
<p><b>Limits:</b> auto-stop is evaluated hypothetically without pilot cancellation. Flight boundaries use the earlier analysis references, not newly confirmed touchdown labels. Each whole-meter pressure-altitude record is treated as any of its 100 possible integer-centimeter values, and all compatible retained-anchor/count states are propagated. At the tabulated candidate stop times, the compatible pressure reconstructions agree. Actual sensor freshness and sampling order were not fully logged, so these remain model predictions, approximately one-second precision, not observed device stops.</p>
<p><a href="auto-stop-proposals.json">Per-track results, source paths and original-file hashes</a></p>'''
(OUT/'auto-stop-proposals.html').write_text(report,encoding='utf-8')
print('Fresh replay complete: six originals unchanged; zero possible in-flight stops for either candidate.')
