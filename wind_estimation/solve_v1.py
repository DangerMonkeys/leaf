from datetime import datetime, timedelta
from math import pow, sqrt
from typing import List, Callable

import numpy as np
from scipy.optimize import minimize

from .estimation import Observation, WindSolve
from .sensor_data import DataPoint, Velocity, compute_velocities

MIN_AIRSPEED = 5
MAX_AIRSPEED = 25
def err_total(vi: List[Velocity], s: WindSolve) -> float:
    err = 0
    for v in vi:
        err += pow(s.airspeed - sqrt(pow(v.dy - s.wy, 2) + pow(v.dx - s.wx, 2)), 2)
    if s.airspeed < MIN_AIRSPEED:
        err *= (MIN_AIRSPEED - s.airspeed + 1)
    if s.airspeed > MAX_AIRSPEED:
        err *= (s.airspeed - MAX_AIRSPEED + 1)
    return err


def err_f(vi: List[Velocity]) -> Callable[[float], float]:
    return lambda x: err_total(vi, WindSolve(airspeed=x[0], wx=x[1], wy=x[2]))


# Samples to use to compute wind speed and direction
T_WINDOW = timedelta(seconds=60)


def solve_wind(points: List[DataPoint], frame_times: List[timedelta]) -> List[Observation]:
    print("Solving for wind...")
    t0 = datetime.now()
    vi: List[Velocity] = compute_velocities(points)
    frames: List[Observation] = []
    for t in frame_times:
        sample_indices = [i for i in range(len(points)) if t - T_WINDOW <= points[i].time <= t]
        frame_vi = [vi[i] for i in sample_indices]
        result = minimize(err_f(frame_vi), np.array([10, 0, 0]), method='Nelder-Mead')
        s = WindSolve(airspeed=result.x[0], wx=result.x[1], wy=result.x[2])
        frames.append(Observation(
            t=t,
            vi=frame_vi,
            s=s,
            recent_data_point=points[sample_indices[-1]],
        ))
    print(f"Solved {len(frames)} wind points in {(datetime.now() - t0).total_seconds():.1f}s")
    return frames
