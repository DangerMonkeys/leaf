import csv, hashlib, json, subprocess
from pathlib import Path
from datetime import datetime, timezone
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'test-data/field-testing/sd-full/2026-11-19-3.2.8'
checked = 0
for manifest in OUT.glob('*-card.json'):
    label = manifest.stem.removesuffix('-card')
    for entry in json.loads(manifest.read_text())['files']:
        data = (OUT/'card-captures'/label/entry['path']).read_bytes()
        assert len(data) == entry['bytes'], entry
        assert hashlib.sha256(data).hexdigest() == entry['sha256'], entry
        checked += 1
before = json.loads((OUT/'sd-before.json').read_text())
final = json.loads((OUT/'65-final-full-card.json').read_text())
normal = lambda items: {entry['path'].replace('\\','/'):entry['sha256'] for entry in items}
assert normal(before['files']) == normal(final['files'])
assert final['free_after'] == 0
control = OUT/'card-captures/41-recovery-results'
last = OUT/'card-captures/63-near-results'
runtime = json.loads((control/'diagnostics/runtime_1.json').read_text())
logbook = json.loads((control/'logbook/2026-11-19_18-16-25_2b8b45b2.json').read_text())
assert logbook['complete']
stack = list(csv.DictReader((control/'diagnostics/stack_1.csv').read_text().splitlines()))
assert any(row['task']=='loop' and int(row['min_unused_bytes'])>0 for row in stack)
assert (last/'diagnostics/stack_2.csv').stat().st_size == 0
assert 'Stack monitor saved /diagnostics/stack_2.csv' in (OUT/'57-near-diagnostics.serial.txt').read_text()
assert (last/'routes/active.json').stat().st_size == 0
assert 'FATAL ERROR: Lock acquisition failed in MessageBus::receive' in (OUT/'21-flight-shutdown.serial.txt').read_text()
igc = []
for path in sorted((last/'tracks').glob('*.igc')):
    result = subprocess.run([str(ROOT/'utils/igc_validator/dist/vali-xlv.exe'),str(path)],capture_output=True,text=True)
    lines = path.read_text().splitlines()
    igc.append({'file':path.name,'bytes':path.stat().st_size,'b_records':sum(line.startswith('B') for line in lines),'g_records':sum(line.startswith('G') for line in lines),'validator_exit':result.returncode,'validator_output':result.stdout.strip(),'validator_stderr':result.stderr.strip()})
assert igc[-1]['validator_exit'] == 0
assert igc[-2]['validator_exit'] == 0
candidate = json.loads((OUT/'candidate.json').read_text())
assert hashlib.sha256((ROOT/'.pio/build/leaf_3_2_8_bench/firmware.bin').read_bytes()).hexdigest() == candidate['firmware.bin']['sha256']
summary = {'validated_at_utc':datetime.now(timezone.utc).isoformat(),'archived_file_copies_verified':checked,'original_files_restored':True,'final_free_bytes':final['free_after'],'control_logbook_complete':logbook['complete'],'control_runtime_json_valid':True,'control_stack_csv_valid':True,'runtime_capture_bytes':runtime['capture_bytes'],'runtime_reserve_bytes':32768,'runtime_start_threshold_bytes':runtime['capture_bytes']+32768,'detected_stack_false_success':True,'detected_navigation_truncation':True,'detected_replay_shutdown_fatal':True,'igc':igc}
(OUT/'validation.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
