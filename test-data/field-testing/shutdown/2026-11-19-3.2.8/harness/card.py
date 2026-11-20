"""Operate only on the identified Leaf test card and its disposable PROBE filler."""
import ctypes, hashlib, json, os, sys
from datetime import datetime, timezone
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'test-data/field-testing/shutdown/2026-11-19-3.2.8'
card = Path('F:/')
k = ctypes.windll.kernel32
serial = ctypes.c_ulong()
name, fs = ctypes.create_unicode_buffer(261), ctypes.create_unicode_buffer(261)
if not k.GetVolumeInformationW(str(card),name,261,ctypes.byref(serial),None,None,fs,261):
    raise ctypes.WinError()
if name.value != 'LEAF VARIO' or serial.value != 0x01114FC5 or fs.value != 'FAT32':
    raise RuntimeError('Connected volume does not match the backed-up Leaf test card')
def free():
    value = ctypes.c_ulonglong()
    if not k.GetDiskFreeSpaceExW(str(card),ctypes.byref(value),None,None): raise ctypes.WinError()
    return value.value
operation, label = sys.argv[1:3]
before = free()
probe = (card/'LEAF_SD_FULL_TEST/PROBE.bin').resolve()
if str(probe.parent).lower() != 'f:\\leaf_sd_full_test' or not probe.is_file():
    raise RuntimeError('Unexpected test filler path')
if operation == 'release':
    with probe.open('r+b') as f:
        f.truncate(0)
        f.flush()
        os.fsync(f.fileno())
elif operation == 'fill':
    reserve = int(sys.argv[3])
    remaining = max(0, free()-reserve)
    block = bytes(128*1024)
    with probe.open('ab') as f:
        while remaining:
            size = min(remaining,len(block))
            written = f.write(block[:size])
            if written != size: raise RuntimeError('Short filler write')
            remaining -= written
        f.flush()
        os.fsync(f.fileno())
elif operation == 'fixtures':
    original = Path('C:/Users/oxoth/projects/leaf/.pio/sd-full-test-2026-11-19/originals/routes/active.json').read_bytes()
    if hashlib.sha256(original).hexdigest() != '30ca8568c00881b56c232496831c8388a208701c5a9e1946d8c11735a26d2a69':
        raise RuntimeError('Original navigation backup hash mismatch')
    data_files = {
        'routes/active.json': original,
        'routes/bench-sd-full.json': (ROOT/'test-data/field-testing/sd-full/2026-11-19-3.2.8/card-captures/63-near-results/routes/bench-sd-full.json').read_bytes(),
        'routes/bench-sd-import.json': (ROOT/'test-data/field-testing/sd-full/2026-11-19-3.2.8/card-captures/63-near-results/routes/bench-sd-import.json').read_bytes(),
        'profiles/profiles.json': (ROOT/'.pio/sd-full-test/profiles.json').read_bytes(),
        'waypoints/user_waypoints.json': json.dumps({'schema':'leaf.user_waypoints','schema_version':'v0.1.0','points':[{'name':'SD baseline point','lat':47.5,'lon':-122.2,'alt_m':100}]}).encode(),
    }
    for directory in ('profiles','diagnostics','buslogs'):
        (card/directory).mkdir(exist_ok=True)
    for relative,data in data_files.items():
        with (card/relative).open('wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
elif operation == 'restore':
    originals = json.loads((OUT/'sd-before.json').read_text())['files']
    original_paths = {entry['path'].replace('\\','/') for entry in originals}
    captured = {}
    for manifest in sorted(OUT.glob('*-card.json')):
        for entry in json.loads(manifest.read_text()).get('files',[]):
            captured[entry['path'].replace('\\','/')] = entry
    for relative, entry in captured.items():
        if relative in original_paths: continue
        path = (card/relative).resolve()
        if not path.is_relative_to(card.resolve()): raise RuntimeError('Unsafe cleanup path')
        if path.is_file():
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
                raise RuntimeError('File changed since capture: '+relative)
            path.unlink()
    backup = Path('C:/Users/oxoth/projects/leaf/.pio/sd-full-test-2026-11-19/originals')
    for entry in originals:
        relative = entry['path'].replace('\\','/')
        data = (backup/relative).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']: raise RuntimeError('Backup mismatch')
        with (card/relative).open('wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
    for relative in ('profiles','diagnostics','buslogs'):
        path = (card/relative).resolve()
        if path.parent != card.resolve(): raise RuntimeError('Unsafe test directory cleanup')
        if path.is_dir() and not any(path.iterdir()): path.rmdir()
elif operation != 'snapshot':
    raise ValueError(operation)
files = []
for folder in ('routes','waypoints','profiles','logbook','tracks','diagnostics','buslogs'):
    for path in sorted((card/folder).rglob('*')):
        if not path.is_file(): continue
        data = path.read_bytes()
        relative = path.relative_to(card)
        target = OUT/'card-captures'/label/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(data)
        entry = {'path':relative.as_posix(),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
        if path.suffix in ('.json','.tmp','.bak'):
            try: json.loads(data); entry['valid_json'] = True
            except ValueError: entry['valid_json'] = False
        files.append(entry)
result = {'utc':datetime.now(timezone.utc).isoformat(),'operation':operation,'volume_serial':f'{serial.value:08X}','free_before':before,'free_after':free(),'files':files}
(OUT/(label+'-card.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
