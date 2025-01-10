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
    solve: WindSolve

    @property
    def lat(self) -> float:
        return self.points[-1].lat

    @property
    def lng(self) -> float:
        return self.points[-1].lng

    @property
    def alt(self) -> float:
        return self.points[-1].alt

    @property
    def ground_speed(self) -> float:
        return self.points[-1].ground_track.speed

    @property
    def track_angle(self) -> float:
        return self.points[-1].ground_track.direction
