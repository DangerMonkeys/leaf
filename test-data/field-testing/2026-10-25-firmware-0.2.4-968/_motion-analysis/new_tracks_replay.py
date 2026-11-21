from pathlib import Path
import ast, json, re, statistics, csv
OUT=Path(__file__).resolve().parent
ROOT=OUT.parent
# Reuse the existing parser without executing its original batch analysis.
module=ast.parse((OUT/'analyze.py').read_text())
functions=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ('fields','number','parse','hh')]
import hashlib
exec(compile(ast.Module(body=functions,type_ignores=[]),'parser','exec'))
tracks=[]
with (OUT/'new-tracks-input.txt').open('w') as stream:
 for ident,p in enumerate(sorted((ROOT/'unknown hardware').glob('*.igc'))):
  d=parse(p); d['id']=ident
  d['pilot']=next(x.split(':',1)[1] for x in p.read_text(errors='replace').splitlines() if x.startswith('HFPLT'))
  first_b=next(b for b in d['b'] if 'ms' in b)
  d['offset']=first_b['utc']-first_b['ms']/1000-25200
  start=int(d['start']['T_MS']); samples=[]
  for x in d['pre']:
   flag=int(x['F'],16)
   samples.append([start+int(x['DT_MS']),float(x['V'])*.44704,float(x['C']),0,0,0,flag&3])
  bs={int(b['ms']):b for b in d['b'] if 'ms' in b}
  for g in d['gl']:
   ms=int(g['T']); b=bs.get(ms)
   if b is None: continue
   flags=(int(g['SV']=='1')) | (2*int(g['CV']=='1' and 'VAR' in b)) | 4 | (8*int(b['valid']))
   samples.append([ms,float(g['S']),b.get('VAR',0)/10,b['alt'],b['lat'],b['lon'],flags])
  samples=sorted({s[0]:s for s in samples}.values())
  d['samples']=samples
  for s in samples: stream.write(' '.join(str(v) for v in [ident]+s)+'\n')
  tracks.append(d)
(OUT/'new-tracks-parsed.json').write_text(json.dumps(tracks))
if (OUT/'new-tracks-results.csv').exists():
 results=list(csv.DictReader((OUT/'new-tracks-results.csv').open()))
 report=[]
 for d in tracks:
  rr=[{k:float(v) for k,v in r.items()} for r in results if int(r['id'])==d['id']]
  local_by_ms={int(b['ms']):b['utc']-25200 for b in d['b'] if 'ms' in b}
  def local(ms): return local_by_ms.get(int(ms),ms/1000+d['offset'])
  first=next((r for r in rr if r['triggered']),None)
  record={'file':d['name'],'pilot':d['pilot'],'start_local':hh(d['b'][0]['utc']-25200),'end_local':hh(d['b'][-1]['utc']-25200),'record_count':len(d['b']),'first_trigger':first,'first_trigger_local':hh(local(first['ms'])) if first else None,'old_start':d['start'],'old_end':d['end'],'paths':{}}
  for mask,label in [(1,'forward'),(2,'vertical'),(4,'blended')]:
   hit=next((r for r in rr if int(r['triggered'])&mask),None)
   record['paths'][label]={'local':hh(local(hit['ms'])),'seconds_after_old_start':round((hit['ms']-int(d['start']['T_MS']))/1000,3),'window':hit} if hit else None
  if d['id']==0:
   ground=[r for r in rr if local(r['ms'])<14*3600+3*60+9 and r['triggered']]
   record['provisional_prelaunch_triggers']=[dict(r,local=hh(local(r['ms']))) for r in ground]
  report.append(record)
  print(json.dumps(record,ensure_ascii=False))
  print('FIRST 100 RECORDS: local speed_mph VAR pressure_alt candidate trigger')
  by_ms={int(r['ms']):r for r in rr}
  for s in d['samples']:
   if s[0]<int(d['start']['T_MS']) or s[0]>int(d['start']['T_MS'])+100000:continue
   r=by_ms[s[0]]
   print(hh(s[0]/1000+d['offset']),round(s[1]/.44704,2),round(s[2],2),s[3],int(r['candidates']),int(r['triggered']))
  print('MINUTE SUMMARY: local min_speed mean_speed max_speed min_alt max_alt trigger_count')
  groups={}
  for s in d['samples']:
   if not s[6]&4:continue
   groups.setdefault(int((s[0]/1000+d['offset'])//60),[]).append(s)
  for minute,ss in groups.items():
   print(hh(minute*60),round(min(s[1] for s in ss)/.44704,1),round(statistics.mean(s[1] for s in ss)/.44704,1),round(max(s[1] for s in ss)/.44704,1),min(s[3] for s in ss),max(s[3] for s in ss),sum(bool(by_ms[s[0]]['triggered']) for s in ss))
 (OUT/'new-tracks-summary.json').write_text(json.dumps(report,indent=2,ensure_ascii=False))
