from dataclasses import dataclass
from datetime import timedelta

from typing import List

from wind_estimation.vectors import Velocity


@dataclass(frozen=True)
class DataPoint:
    time: timedelta
    """Time past start of sampling that this data point was sampled."""

    ground_track: Velocity
    """Ground track"""

    lng: float
    """Longitude, degrees east of the Prime Meridian"""

    lat: float
    """Latitude, degrees north of the equator"""

    alt: float
    """Altitude, meters above the WGS84 ellipsoid"""


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
        v = Velocity.from_speed_and_direction(float(cols[1]), float(cols[0]))
        points.append(DataPoint(
            time=timedelta(seconds=r),
            ground_track=v,
            lng=float(cols[2]),
            lat=float(cols[3]),
            alt=float(cols[4]),
        ))

    return points
