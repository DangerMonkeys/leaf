from dataclasses import dataclass
from datetime import timedelta
from typing import List

from leaf.wind_estimation.sensor_data import Velocity, DataPoint


@dataclass
class WindSolve:
    wind: Velocity
    airspeed: float


@dataclass
class Observation:
    t: timedelta
    points: List[DataPoint]
    most_recent: DataPoint
    solve: WindSolve

    @property
    def lat(self) -> float:
        return self.most_recent.lat

    @property
    def lng(self) -> float:
        return self.most_recent.lng

    @property
    def alt(self) -> float:
        return self.most_recent.alt

    @property
    def ground_speed(self) -> float:
        return self.most_recent.ground_track.speed

    @property
    def track_angle(self) -> float:
        return self.most_recent.ground_track.direction
