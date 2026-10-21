"""Compare the actual legacy gravity and SFLP/bias estimator paths in leafsim.

Run on the host after building sim/build328/leafsim in Docker:
  python sim/tests/imu_filter_comparison.py

Uses consistent position/velocity/acceleration, integer pressure quantization, a
moving start, sustained acceleration, and a change in projected sensor bias.
This is a controlled estimator test, not a model of either chip's attitude errors.
Writes inputs, full output, and a JSON summary under ignored sim/build/.
"""
import json
import math
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "sim/build"
OUT.mkdir(exist_ok=True)
G = 9.80665


def motion(t):
    position, velocity = 100.0, 2.0
    for begin, end, acceleration in [(0, 20, 0), (20, 40, .3), (40, 50, 0),
                                     (50, 70, -.4), (70, 160, 0)]:
        dt = min(max(t - begin, 0), end - begin)
        position += velocity * dt + .5 * acceleration * dt * dt
        if t < end:
            return position, velocity + acceleration * dt, acceleration
        velocity += acceleration * dt
    return position, velocity, 0


windows = {
    "moving_start_6_to_10s": (6, 10),
    "sustained_accel_25_to_39s": (25, 39),
    "steady_climb_43_to_49s": (43, 49),
    "stationary_85_to_100s": (85, 100),
    "bias_step_recovery_140_to_155s": (140, 155),
}
reports = {}
for mode in ("legacy", "bias"):
    name = f"imu-compare-{mode}"
    events = []
    for ms in range(0, 160001, 50):
        t = ms / 1000
        altitude, velocity, acceleration = motion(t)
        # Same 1 Pa rounding as the device's pressure messages. No random noise:
        # this isolates correction behavior; noisy real-board data is a separate test.
        pressure = round(101325 * (1 - altitude / 44330) ** (1 / .19029495))
        bias = -.012 if t < 110 else .008
        az = 1 + bias + acceleration / G
        suffix = ",V,0,0,1,0" if mode == "bias" else ""
        events.extend([f"P{ms},{pressure}", f"M{ms},A,0,0,{az},Q,0,0,0{suffix}"])
    (OUT / f"{name}.log").write_text("\n".join(events) + "\n")
    times = [i / 2 for i in range(12, 319)]
    steps = ["1 scenario seek 0", "1 scenario play"]
    steps += [f"{t+1:g} expect-climb -100 100" for t in times]
    steps += ["161 quit"]
    (OUT / f"{name}.txt").write_text("\n".join(steps) + "\n")
    result = subprocess.run([
        "docker", "run", "--rm", "-v", f"{ROOT}:/leaf", "-w", "/leaf", "gcc:13",
        "./sim/build328/leafsim", "--port", "0", "--speed", "0",
        "--scenario", f"sim/build/{name}.log", "--script", f"sim/build/{name}.txt",
    ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
    (OUT / f"{name}-output.txt").write_text(result.stdout)
    measured = [float(v) for v in re.findall(r"script: ok climb ([-+\d.e]+) in", result.stdout)]
    assert len(measured) == len(times), (mode, len(measured), len(times))
    stats = {}
    for label, (begin, end) in windows.items():
        errors = [value - motion(t)[1] for t, value in zip(times, measured) if begin <= t <= end]
        stats[label] = {"mean_error_mps": sum(errors) / len(errors),
                        "rms_error_mps": math.sqrt(sum(e*e for e in errors) / len(errors)),
                        "max_abs_error_mps": max(abs(e) for e in errors)}
    reports[mode] = stats

(OUT / "imu-filter-comparison.json").write_text(json.dumps(reports, indent=2) + "\n")
print(json.dumps(reports, indent=2))
# The bias path must meet a mean resting error of 0.05 m/s, and preserve sustained
# acceleration to within 0.15 m/s RMS in this deterministic, synchronized test.
for label, stats in reports["bias"].items():
    assert stats["rms_error_mps"] < .15, (label, stats)
    if "stationary" in label or "bias_step" in label:
        assert abs(stats["mean_error_mps"]) < .05, (label, stats)
