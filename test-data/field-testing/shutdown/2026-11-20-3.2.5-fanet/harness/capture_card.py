import ctypes,hashlib,json,shutil,sys
from datetime import datetime,timezone
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'test-data/field-testing/shutdown/2026-11-20-3.2.5-fanet'
card=Path('F:/')
k=ctypes.windll.kernel32
name,fs=ctypes.create_unicode_buffer(261),ctypes.create_unicode_buffer(261)
serial=ctypes.c_ulong()
if not k.GetVolumeInformationW(str(card),name,261,ctypes.byref(serial),None,None,fs,261):raise ctypes.WinError()
assert name.value=='LEAF VARIO' and fs.value=='FAT32'
capacity=shutil.disk_usage(card)
assert capacity.total==8047788032,'Unexpected card capacity'
identity=out/'volume-identity.json'
value={'serial':f'{serial.value:08X}','label':name.value,'filesystem':fs.value,'capacity':capacity.total}
if identity.exists():assert json.loads(identity.read_text())==value
else:identity.write_text(json.dumps(value,indent=2)+'\n')
label=sys.argv[1]
manifest={'utc':datetime.now(timezone.utc).isoformat(),'identity':value,'free_bytes':capacity.free,'inventory':[],'files':[]}
for folder in ('tracks','logbook','diagnostics','fanetlogs','routes','waypoints','profiles'):
 for path in sorted((card/folder).rglob('*')):
  if not path.is_file():continue
  relative=path.relative_to(card).as_posix()
  manifest['inventory'].append({'path':relative,'bytes':path.stat().st_size})
  selected=(folder in ('tracks','logbook','fanetlogs') and '2026-11-20' in path.name) or (folder=='diagnostics' and path.name.startswith('stack_') and int(path.stem.split('_')[-1])>=4) or folder in ('routes','waypoints','profiles')
  if not selected:continue
  assert path.stat().st_size<16*1024*1024,'Unexpected large selected file'
  data=path.read_bytes();target=out/'card-captures'/label/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
  manifest['files'].append({'path':relative,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
(out/(label+'-card.json')).write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'identity':value,'free_bytes':capacity.free,'files':manifest['files']},indent=2))
