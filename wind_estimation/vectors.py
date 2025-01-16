from __future__ import annotations
from dataclasses import dataclass
from math import atan2, cos, degrees, radians, sin, sqrt


@dataclass(frozen=True)
class Velocity:
    dx: float
    """Eastward speed, meters per second"""

    dy: float
    """Northward speed, meters per second"""

    @property
    def speed(self) -> float:
        return sqrt(pow(self.dx, 2) + pow(self.dy, 2))

    @property
    def direction(self) -> float:
        return degrees(atan2(self.dx, self.dy)) % 360

    def __add__(self, other):
        if isinstance(other, Velocity):
            return Velocity(dx=self.dx + other.dx, dy=self.dy + other.dy)
        raise ValueError(f"Cannot add {type(other).__name__} to Velocity")

    def __sub__(self, other):
        if isinstance(other, Velocity):
            return Velocity(dx=self.dx - other.dx, dy=self.dy - other.dy)
        raise ValueError(f"Cannot subtract {type(other).__name__} from Velocity")

    @staticmethod
    def from_speed_and_direction(speed: float, direction: float) -> Velocity:
        return Velocity(
            dx=speed * sin(radians(direction)),
            dy=speed * cos(radians(direction))
        )
