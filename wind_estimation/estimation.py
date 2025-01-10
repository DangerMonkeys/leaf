from dataclasses import dataclass
from datetime import timedelta
from typing import List

from leaf.wind_estimation.sensor_data import Velocity


@dataclass
class WindSolve:
    airspeed: float
    wx: float
    wy: float


@dataclass
class Frame:
    t: timedelta
    vi: List[Velocity]
    s: WindSolve
    lat: float
    lng: float
    alt: float
    wind_speed: float
    wind_direction: float
    ground_speed: float
    track: float
