from pathlib import Path
import json,ast
OUT=Path(__file__).resolve().parent
tree=ast.parse((OUT/'threshold_sweep.py').read_text())
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('hh','stop_bounds')],type_ignores=[]),'stop_functions','exec'))
OLD=OUT.parent.parent/'2026-10-25-firmware-0.2.4-968/_motion-analysis'
data=[d for d in json.loads((OLD/'features.json').read_text()) if 'launch' in d]
friends=[d for d in json.loads((OLD/'new-tracks-parsed.json').read_text()) if len(d['b'])>100]
result=[]
for d in data+friends:
 new=d in friends
 launch=d['launch'] if not new else 14*3600+3*60+(10 if d['id']==0 else 15)
 land=d['land'] if not new else 14*3600+50*60+(15 if d['id']==0 else 5)
 gs={int(g['T']):g for g in d['gl']};ga={int(g['T']):g for g in d['ga']}
 rows=[]
 for b in d['b']:
  if 'ms' not in b:continue
  ms=int(b['ms']);g=gs.get(ms);a=ga.get(ms,{})
  if not g:continue
  local=b['utc']-25200
  if local<launch:continue
  rows.append(dict(ms=ms,local=local,alt=b['alt'],flags=int(g['SV']=='1')|4|(8*int(b['valid'])),s=float(g['S']),raw=float(a.get('R','nan')),raw_valid=a.get('RV')=='1'))
 label=(d['pilot'] if new else d['unit'])
 for n,speed,alt in [(20,3,1),(15,3,2),(10,3,2),(10,4,2),(15,4,2)]:
  r=stop_bounds(rows,n,speed,alt,land=land)
  item=dict(label=label,checks=n,speed_mph=speed,altitude_m=alt,**r)
  result.append(item);print(json.dumps(item))
(OUT/'earlier-stop-sweep.json').write_text(json.dumps(result,indent=2))
