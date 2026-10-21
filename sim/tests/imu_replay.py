"""Generate a deterministic SFLP recording and estimator assertions.

Run from repo root, then use the printed leafsim command. Fixtures go in sim/build.
"""
from pathlib import Path
import sys

bias_g = float(sys.argv[1]) if len(sys.argv) > 1 else 0
name = 'imu-sflp-biased' if bias_g else 'imu-sflp'

out = Path("sim/build")
out.mkdir(exist_ok=True)
events = []
for t in range(0, 95001, 50):
    # Already climbing at power-on. No stationary initialization segment.
    # Physically consistent deceleration from 2 m/s to rest over four seconds.
    stop_time = min(max(t / 1000 - 35, 0), 4)
    altitude = 100 + 2 * min(t / 1000, 35) + 2 * stop_time - .25 * stop_time**2
    acceleration = -.5 if 35000 <= t < 39000 else 0
    pressure = round(101325 * (1 - altitude / 44330) ** (1 / .19029495))
    if not 18000 <= t < 19000:
        events.append(f"P{t},{pressure}")
    if 12000 <= t < 13000:
        continue
    if t == 22000:
        events.append(f"M{t},a,0,0,0,q,0,0,0,v,0,0,0,1")
        continue
    # Arbitrary tilt followed by an inverted mounting, with matching gravity/specific force.
    gx, gz = (.6, .8) if t < 24000 else (0, -1)
    if 26000 <= t < 27000:
        gz = 0  # Invalid zero gravity must invalidate the filter immediately.
    stamp = 30000 if 30000 <= t < 31000 else t
    specific_force = 1 + bias_g + acceleration / 9.80665
    events.append(f"M{stamp},A,{gx*specific_force},0,{gz*specific_force},q,0,0,0,V,{gx},0,{gz},0")

(out / f"{name}.log").write_text("\n".join(events) + "\n")
# Start playback at an explicit device time; setup itself advances the simulated clock.
checks = """8 expect-climb 1.7 2.3
12.6 expect-climb invalid
16 expect-climb 1.7 2.3
18.6 expect-climb invalid
21 expect-climb 1.7 2.3
22.2 expect-climb invalid
25 expect-climb 1.7 2.3
26.5 expect-climb invalid
29 expect-climb 1.7 2.3
30.6 expect-climb invalid
34 expect-climb 1.7 2.3
50 expect-climb -0.05 0.05
65 expect-climb -0.05 0.05
80 expect-climb -0.05 0.05
94 expect-climb -0.05 0.05
94.5 quit
"""
steps = ['1 scenario seek 0', '1 scenario play']
for check in checks.splitlines():
    when, action = check.split(' ', 1)
    steps.append(f'{float(when) + 1:g} {action}')
(out / f"{name}.txt").write_text('\n'.join(steps) + '\n')
print(f"./sim/build/leafsim --port 0 --speed 0 --scenario sim/build/{name}.log --script sim/build/{name}.txt")
