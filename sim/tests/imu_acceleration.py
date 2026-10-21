"""Generate an SFLP high-acceleration continuity regression for the real instrument path.

The motion is synthetic and synchronized: this checks validity during acceleration,
not physical SFLP accuracy or the hardware FIFO's saturation rejection.
"""

import math
from pathlib import Path

out = Path("sim/build")
out.mkdir(exist_ok=True)
events = []
position = 100.0
velocity = 0.0
dt = 0.05
for t in range(0, 20001, 50):
    # Repeated signed vertical acceleration, reaching +6 g specific force.
    acceleration = 5 * 9.80665 * math.sin(2 * math.pi * (t - 5000) / 1000) if 5000 <= t < 13000 else 0
    pressure = round(101325 * (1 - position / 44330) ** (1 / .19029495))
    events.extend([
        f"P{t},{pressure}",
        f"M{t},A,0,0,{1+acceleration/9.80665},q,0,0,0,V,0,0,1,0",
    ])
    position += velocity * dt + .5 * acceleration * dt**2
    velocity += acceleration * dt

log = out / "imu-acceleration.log"
script = out / "imu-acceleration.txt"
log.write_text("\n".join(events) + "\n")
steps = ["1 scenario seek 0", "1 scenario play"]
# Every instrument update during and after the maneuvers must retain valid climb.
steps.extend(f"{(t+1000)/1000:g} expect-climb -100 100" for t in range(5000, 15001, 50))
steps.append("20 quit")
script.write_text("\n".join(steps) + "\n")
print(f"./sim/build328/leafsim --port 0 --speed 0 --scenario {log.as_posix()} --script {script.as_posix()}")
