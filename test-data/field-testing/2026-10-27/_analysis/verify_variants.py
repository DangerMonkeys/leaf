from pathlib import Path
OUT=Path(__file__).resolve().parent
source=(OUT/'variants/baseline/logging/flight_auto_start.h').read_text()
variants={'baseline':source,
 'forward7':source.replace('const bool forwardCandidate = forward >= 1;', 'const bool forwardCandidate = forward >= 1 && window_.meanSpeedMps >= 3.12928f;'),
 'distance11':source.replace('const bool forwardCandidate = forward >= 1;', 'const bool forwardCandidate = forward >= 1 && window_.displacementM >= 11.0f;'),
 'shared7':source.replace('FORWARD_SPEED_MPS = 2.2352f','FORWARD_SPEED_MPS = 3.12928f')}
confirmation=source.replace('int8_t sign = 0) {','int8_t sign = 0, uint32_t confirmMs = CONFIRM_MS) {').replace('>= CONFIRM_MS;', '>= confirmMs;').replace('forwardConfirmation_.update(nowMs, forwardCandidate)','forwardConfirmation_.update(nowMs, forwardCandidate, 0, 5000)')
assert confirmation!=source
variants['forward5s']=confirmation
for name,text in variants.items():
 p=OUT/'variants'/name/'logging/flight_auto_start.h';p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
old=OUT/'historical-replay-input.txt'
friends=OUT.parent.parent/'2026-10-25-firmware-0.2.4-968/_motion-analysis/new-tracks-input.txt'
with (OUT/'combined-input.txt').open('w') as f:
 f.write((OUT/'replay-input.txt').read_text())
 for line in old.read_text().splitlines():
  values=line.split();assert len(values)==9
  values[0]=str(int(values[0])+100)
  f.write(' '.join(values[:-1])+'\n')
 for line in friends.read_text().splitlines():
  values=line.split();assert len(values)==8
  values[0]=str(int(values[0])+200)
  f.write(' '.join(values)+'\n')
print('Analysis-only header copies prepared; original firmware unchanged.')
