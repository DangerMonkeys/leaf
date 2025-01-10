from datetime import datetime, timedelta
from math import atan2,degrees, sqrt
from typing import List, Callable

import numpy as np
from scipy.optimize import minimize

from .estimation import Frame, WindSolve
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


# Number of trailing samples to use to compute wind speed and direction
N_WINDOW = 60

# How many samples to move forward each animation frame / visualization time step
N_STEP = 5

# Number of animation frames / visualization time steps to compute and show
N_STEPS = int(15 * 60 / N_STEP)

# Tim
START_AT = 30 * 60


def solve_wind(points: List[DataPoint]) -> List[Frame]:
    print("Solving for wind...")
    t0 = datetime.now()
    vi: List[Velocity] = compute_velocities(points)
    frames: List[Frame] = []
    prev_x = np.array([10, 0, 0])
    for i1 in range(START_AT + 1, START_AT + N_STEPS * N_STEP + 2, N_STEP):
        i0 = i1 - N_WINDOW + 1
        result = minimize(err_f(vi[i0:i1]), np.array([10, 0, 0]), method='Nelder-Mead')
        prev_x = result.x
        p = points[i1]
        s = WindSolve(airspeed=result.x[0], wx=result.x[1], wy=result.x[2])
        frames.append(Frame(
            t=timedelta(seconds=i1),
            vi=vi[i0:i1],
            s=s,
            lat=p.lat,
            lng=p.lng,
            alt=p.alt,
            wind_speed=sqrt(pow(s.wx, 2) + pow(s.wy, 2)),
            wind_direction=degrees(atan2(s.wx, s.wy)),
            ground_speed=p.speed,
            track=p.heading,
        ))
    print(f"Solved {len(frames)} wind points in {(datetime.now() - t0).total_seconds():.1f}s")
    return frames
