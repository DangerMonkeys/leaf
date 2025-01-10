from dataclasses import dataclass
from math import cos, sin, radians

from typing import List


@dataclass
class DataPoint:
    heading: float
    speed: float
    lat: float
    lng: float
    alt: float


@dataclass
class Velocity:
    dx: float
    dy: float


def load_sensor_data(csv_file_name: str) -> List[DataPoint]:
    with open(csv_file_name, "r") as f:
        lines = f.readlines()

    points: List[DataPoint] = []
    for line in lines[1:]:
        cols = line.split(',')
        points.append(DataPoint(*[float(c) for c in cols]))

    return points


def compute_velocities(points: List[DataPoint]) -> List[Velocity]:
    return [
        Velocity(
            dx=p.speed * sin(radians(p.heading)),
            dy=p.speed * cos(radians(p.heading))
        ) for p in points
    ]
