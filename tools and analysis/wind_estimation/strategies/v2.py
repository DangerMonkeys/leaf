from dataclasses import dataclass
from datetime import timedelta
from math import pow, sqrt
from typing import List

import numpy as np
from scipy.optimize import minimize

from wind_estimation.estimation import Observation, WindSolve
from wind_estimation.sensor_data import DataPoint, Velocity


@dataclass
class Bin:
    lo: float
    hi: float
    m_points: int
    points: List[DataPoint]

    def includes(self, air_track_angle: float) -> bool:
        if self.lo < self.hi:
            return self.lo <= air_track_angle < self.hi
        else:
            return air_track_angle >= self.lo or air_track_angle < self.hi

    def add(self, point: DataPoint) -> None:
        if len(self.points) >= self.m_points:
            # Remove oldest point
            oldest = None
            for p in self.points:
                if oldest is None or p.time < oldest.time:
                    oldest = p
            self.points.remove(oldest)
        self.points.append(point)

    def add_if_applicable(self, point: DataPoint, wind: Velocity) -> None:
        air_velocity = point.ground_track - wind
        if self.includes(air_velocity.direction):
            self.add(point)


def collect_points(bins: List[Bin]) -> List[DataPoint]:
    points = set()
    for bin in bins:
        for point in bin.points:
            points.add(point)
    return list(points)


P_BINS = 6
"""Number of pie slices for direction-based bins"""

M_PER_BIN = 6
"""Number of most recent points to keep per direction-based bin"""

BINNED_LAYERS = 2
"""Number of offset layers of direction-based bins"""

N_RECENT = 30
"""Number of most recent points to keep, regardless of direction"""


def make_bins() -> List[Bin]:
    bins = []
    for layer in range(BINNED_LAYERS):
        theta0 = (360 / P_BINS) * (layer / BINNED_LAYERS)
        bins += [
            Bin(
                lo=theta0 + p * 360 / P_BINS,
                hi=theta0 + (p + 1) * 360 / P_BINS,
                m_points=M_PER_BIN,
                points=[],
            )
            for p in range(P_BINS)
        ]
    bins.append(Bin(lo=0, hi=360, m_points=N_RECENT, points=[]))
    return bins


def err_total(vi: List[Velocity], s: WindSolve) -> float:
    err = 0
    for v in vi:
        err += pow(s.airspeed - sqrt(pow(v.dy - s.wind.dy, 2) + pow(v.dx - s.wind.dx, 2)), 2)
    return err


def solve_wind(points: List[DataPoint], frame_times: List[timedelta]) -> List[Observation]:
    bins = make_bins()

    observations: List[Observation] = []

    solve = WindSolve(airspeed=10, wind=Velocity(dx=0, dy=0))
    p = 0

    for f, frame_time in enumerate(frame_times):
        while points[p].time <= frame_time:
            # Update bins with this data point
            for bin in bins:
                bin.add_if_applicable(points[p], solve.wind)

            p += 1

        # Perform new solve to fulfill this observation
        solve_points = collect_points(bins)
        frame_vi = [fp.ground_track for fp in solve_points]

        def err_f(x: List[float]) -> float:
            return err_total(frame_vi, WindSolve(airspeed=x[0], wind=Velocity(dx=x[1], dy=x[2])))

        result = minimize(err_f,
                          np.array([solve.airspeed, solve.wind.dx, solve.wind.dy]),
                          method='Nelder-Mead')
        solve = WindSolve(airspeed=result.x[0], wind=Velocity(dx=result.x[1], dy=result.x[2]))

        observations.append(Observation(
            t=frame_time,
            points=solve_points,
            most_recent=points[p],
            solve=solve,
        ))

    return observations
