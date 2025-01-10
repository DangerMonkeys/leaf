from datetime import timedelta
from math import pow, sqrt
from typing import List

import numpy as np
from scipy.optimize import minimize

from wind_estimation.estimation import Observation, WindSolve
from wind_estimation.sensor_data import DataPoint, Velocity

# Samples to use to compute wind speed and direction
T_WINDOW = timedelta(seconds=60)

MIN_AIRSPEED = 5
MAX_AIRSPEED = 25


def err_total(vi: List[Velocity], s: WindSolve) -> float:
    err = 0
    for v in vi:
        err += pow(s.airspeed - sqrt(pow(v.dy - s.wind.dy, 2) + pow(v.dx - s.wind.dx, 2)), 2)
    if s.airspeed < MIN_AIRSPEED:
        err *= (MIN_AIRSPEED - s.airspeed + 1)
    if s.airspeed > MAX_AIRSPEED:
        err *= (s.airspeed - MAX_AIRSPEED + 1)
    return err


def solve_wind(points: List[DataPoint], frame_times: List[timedelta]) -> List[Observation]:
    frames: List[Observation] = []
    for t in frame_times:
        sample_indices = [i for i in range(len(points)) if t - T_WINDOW <= points[i].time <= t]
        frame_vi = [points[i].ground_track for i in sample_indices]

        def err_f(x: List[float]) -> float:
            return err_total(frame_vi, WindSolve(airspeed=x[0], wind=Velocity(dx=x[1], dy=x[2])))

        result = minimize(err_f, np.array([10, 0, 0]), method='Nelder-Mead')
        solve = WindSolve(airspeed=result.x[0], wind=Velocity(dx=result.x[1], dy=result.x[2]))

        frames.append(Observation(
            t=t,
            points=[points[i] for i in sample_indices],
            most_recent=points[sample_indices[-1]],
            solve=solve,
        ))
    return frames
