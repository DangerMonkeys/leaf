import json,sys,time
from datetime import datetime,timezone
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'utils'))
from leaf_bench_console import Console
out=root/'test-data/field-testing/shutdown/2026-11-20-3.2.5-fanet'
report={'utc':datetime.now(timezone.utc).isoformat(),'phone_connection':'user-confirmed after Web App close','samples':[]}
raw=(out/'18-second-shutdown.serial.txt').open('w',encoding='utf-8')
class Tap:
 def __init__(self,port):self.port=port
 def __getattr__(self,name):return getattr(self.port,name)
 def read_until(self,*a,**kw):
  data=self.port.read_until(*a,**kw);raw.write(data.decode('utf-8','replace'));raw.flush();return data
try:
 with Console() as unit:
  unit.port=Tap(unit.port);unit.check_link();report['initial']=unit.status();report['fanet_before']=unit.request('fanet');report['heap_before']=unit.heap()
  assert report['initial']['flight_running'] and report['initial']['power']=='On'
  assert report['fanet_before'].split()[2]=='RUNNING'
  start=time.monotonic();report['hold_reply']=unit.request('hold CENTER 2300')
  while time.monotonic()-start<12:
   status=unit.status();elapsed=time.monotonic()-start
   report['samples'].append({'seconds_after_hold_command':round(elapsed,3),'status':status})
   if status['power']=='OffUSB':
    report['seconds_from_hold_command_to_charging']=round(elapsed,3);break
   time.sleep(0.1)
  report['final']=unit.status();report['fanet_after']=unit.request('fanet');report['heap_after']=unit.heap();report['stacks_after']=unit.request('stacks')
  assert report['final']['power']=='OffUSB' and report['final']['page']=='Charging'
  assert report['heap_after']['internal_min_since_boot']<=report['heap_before']['internal_min_since_boot']
except Exception as error:
 report['error']=repr(error)
finally:
 raw.close();(out/'18-second-shutdown.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
assert 'error' not in report
