import csv,hashlib,json,subprocess,re,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'test-data/field-testing/shutdown/2026-11-20-3.2.5-fanet'
capture=out/'card-captures/22-final-results'
summary={'source_commit':'8a435a426f5383b0097ee96df40cad47ed4ea873','phone_connection_evidence':'User confirmed before first shutdown and after wake and Web App close','flights':[],'fanet_logs':[],'stacks':[]}
for p in sorted((capture/'tracks').glob('*.igc')):
 raw=p.read_bytes();lines=raw.splitlines();g=lines[-1][1:].decode('ascii');computed=hashlib.md5(b''.join(s+b'\n' for s in lines[:-1])).hexdigest().upper()
 run=subprocess.run([str(root/'utils/igc_validator/dist/vali-xlv.exe'),str(p)],capture_output=True,text=True)
 ends=[line.decode('ascii') for line in lines if line.startswith(b'LXLVEND')]
 b=[line for line in lines if line.startswith(b'B')]
 times=[int(s[1:3])*3600+int(s[3:5])*60+int(s[5:7]) for s in b]
 gaps=[(v-u)%86400 for u,v in zip(times,times[1:])]
 result={'path':p.relative_to(out).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'strict_validator_returncode':run.returncode,'strict_validator_output':run.stdout.strip(),'strict_validator_error':run.stderr.strip(),'non_ascii_records':[{'line':i+1,'record':line.decode('utf-8','backslashreplace')} for i,line in enumerate(lines) if any(c<32 or c>126 for c in line)],'raw_g_expected':g,'raw_g_calculated':computed,'raw_g_matches':g==computed,'b_records':len(b),'largest_b_time_gap_seconds':max(gaps,default=0),'end_markers':ends,'timing_diagnostics':[s.decode('ascii') for s in lines if s.startswith((b'LXLVFD ',b'LXLVFDTIME'))]}
 assert result['raw_g_matches'] and b and len(ends)==1 and 'CAUSE=POWER_OFF' in ends[0]
 assert run.returncode==1 and run.stderr.strip()=='non-printable record'
 summary['flights'].append(result)
for p in sorted((capture/'fanetlogs').glob('*.csv')):
 rows=list(csv.DictReader(line for line in p.read_text().splitlines() if not line.startswith('#')))
 summary['fanet_logs'].append({'path':p.relative_to(out).as_posix(),'observations':len(rows),'neighbors':sorted({x['neighbor_id'] for x in rows}),'max_queue_drops':max((int(x['queue_drops']) for x in rows),default=0)})
for p in sorted((capture/'diagnostics').glob('stack_*.csv')):
 summary['stacks'].append({'path':p.relative_to(out).as_posix(),'rows':list(csv.DictReader(p.open()))})
summary['logbook']=[{'path':p.relative_to(out).as_posix(),'complete':(d:=json.loads(p.read_text()))['complete'],'duration_seconds':d['metrics']['duration_seconds'],'track':d.get('track')} for p in sorted((capture/'logbook').glob('*.json'))]
assert len(summary['flights'])==2 and len(summary['logbook'])==2 and all(x['complete'] for x in summary['logbook'])
summary['second_shutdown_seconds_from_hold']=json.loads((out/'18-second-shutdown.json').read_text())['seconds_from_hold_command_to_charging']
summary['final_heap']=json.loads((out/'21-cancel-upload-for-readback.json').read_text())['heap']
summary['raw_checksums_match_but_strict_igc_validation_fails_known_header_issue']=True
(out/'analysis.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
