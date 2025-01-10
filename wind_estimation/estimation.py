from dataclasses import dataclass
from datetime import timedelta
from math import atan2, degrees, pow, sqrt
from typing import List

from leaf.wind_estimation.sensor_data import Velocity, DataPoint


@dataclass
class WindSolve:
    airspeed: float
    wx: float
    wy: float


@dataclass
class Observation:
    t: timedelta
    vi: List[Velocity]
    s: WindSolve
    recent_data_point: DataPoint

    @property
    def lat(self) -> float:
        return self.recent_data_point.lat

    @property
    def lng(self) -> float:
        return self.recent_data_point.lng

    @property
    def alt(self) -> float:
        return self.recent_data_point.alt

    @property
    def wind_speed(self) -> float:
        return sqrt(pow(self.s.wx, 2) + pow(self.s.wy, 2))

    @property
    def wind_direction(self) -> float:
        return degrees(atan2(self.s.wx, self.s.wy))

    @property
    def ground_speed(self) -> float:
        return self.recent_data_point.speed

    @property
    def track_angle(self) -> float:
        return self.recent_data_point.track_angle
