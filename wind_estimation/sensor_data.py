from dataclasses import dataclass
from datetime import timedelta
from math import cos, sin, radians

from typing import List


@dataclass
class DataPoint:
    time: timedelta
    """Time past start of sampling that this data point was sampled."""

    track_angle: float
    """Ground track angle, degrees east of north"""

    speed: float
    """Speed, meters per second"""

    lng: float
    """Longitude, degrees east of the Prime Meridian"""

    lat: float
    """Latitude, degrees north of the equator"""

    alt: float
    """Altitude, meters above the WGS84 ellipsoid"""


@dataclass
class Velocity:
    dx: float
    """Eastward speed, meters per second"""

    dy: float
    """Northward speed, meters per second"""


def load_sensor_data(csv_file_name: str) -> List[DataPoint]:
    """Load sensor data from the specified CSV file.

    Args:
        csv_file_name: Path to CSV containing rows corresponding to DataPoint fields after a header row.

    Returns: Parsed DataPoints for all rows in CSV.
    """
    with open(csv_file_name, "r") as f:
        lines = f.readlines()

    points: List[DataPoint] = []
    for r, line in enumerate(lines[1:]):
        cols = line.split(',')
        points.append(DataPoint(timedelta(seconds=r), *[float(c) for c in cols]))

    return points


def compute_velocities(points: List[DataPoint]) -> List[Velocity]:
    """Compute ground speed velocities at each given DataPoint.

    Args:
        points: DataPoints at which to compute ground speed.

    Returns: One Velocity for each provided DataPoint
    """
    return [
        Velocity(
            dx=p.speed * sin(radians(p.track_angle)),
            dy=p.speed * cos(radians(p.track_angle))
        ) for p in points
    ]
