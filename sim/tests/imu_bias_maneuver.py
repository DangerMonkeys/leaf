"""Exercise bias-only gating, recovery holdoff and clipping metadata through real replay."""
import math
from pathlib import Path

out = Path("sim/build")
out.mkdir(exist_ok=True)
events = []
p, v, dt = 100.0, 0.0, .05
for ms in range(0, 70001, 50):
    t = ms / 1000
    a = 3 * 9.80665 * math.sin(2 * math.pi * t) if 20 <= t < 28 else 0
    disturbance = .5 * math.sin(5 * (t - 20)) if 20 <= t < 28 else 0
    pressure = round(101325 * (1 - (p + disturbance) / 44330) ** (1 / .19029495))
    # A clipping flag on otherwise calm data must also pause learning, independently
    # of the acceleration window. Old records omit this field and remain readable.
    clipped = int(abs(a) > 20 or ms == 32000)
    # Deliberately under-observe maneuver impulses to exercise missing/aliased input,
    # rather than only proving the gate on perfectly synchronized ideal motion.
    reported_a = .65 * a
    az = 1 - .01 + reported_a / 9.80665
    if 46000 <= ms < 47000:
        az = 1.064  # Inside the same manufacturing-tolerance window as legacy ICM learning.
    if 48000 <= ms < 49000:
        az = 1.066  # Outside that shared window, without triggering a validity reset.
    if 54000 <= ms < 55000:
        az = .936
    if 56000 <= ms < 57000:
        az = .934
    ax = 0
    if 62000 <= ms < 63000:
        ax, az = .6, .8  # Total is 1 g, but projected vertical residual is -0.2 g.
    events.extend([f"P{ms},{pressure}", f"M{ms},A,{ax},0,{az},q,0,0,0,V,0,0,1,0,{clipped}"])
    p += v * dt + .5 * a * dt**2
    v += a * dt

(out / "imu-bias-maneuver.log").write_text("\n".join(events) + "\n")
steps = ["1 scenario seek 0", "1 scenario play", "20 expect-bias -0.12 -0.075"]
for ms in range(20500, 29501, 250):
    steps.extend([f"{ms/1000+1:g} expect-climb -100 100",
                  f"{ms/1000+1:g} expect-bias-learning paused",
                  f"{ms/1000+1:g} expect-bias -0.12 -0.075"])
steps.extend(["32 expect-bias-learning active", "33.1 expect-bias-learning paused",
              "34.8 expect-bias-learning paused", "35.2 expect-bias-learning active",
              "44 expect-climb -0.05 0.05", "44 expect-bias -0.12 -0.075",
              "47.5 expect-bias-learning active", "49.5 expect-bias-learning paused",
              "49.5 expect-climb -100 100", "51.5 expect-bias-learning paused",
              "52.2 expect-bias-learning active", "55.5 expect-bias-learning active",
              "57.5 expect-bias-learning paused", "59.5 expect-bias-learning paused",
              "60.2 expect-bias-learning active", "63.5 expect-bias-learning paused",
              "63.5 expect-climb -100 100", "65.5 expect-bias-learning paused",
              "66.2 expect-bias-learning active", "70 quit"])
(out / "imu-bias-maneuver.txt").write_text("\n".join(steps) + "\n")
print("./sim/build328/leafsim --port 0 --speed 0 --scenario sim/build/imu-bias-maneuver.log --script sim/build/imu-bias-maneuver.txt")
