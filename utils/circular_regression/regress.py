from dataclasses import dataclass
from datetime import datetime, timedelta
from math import cos, sin, radians, sqrt, pow, degrees, atan2
from typing import List, Callable

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.animation import FuncAnimation
from scipy.optimize import minimize


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


points: List[DataPoint] = []

with open('example1.csv', 'r') as f:
    lines = f.readlines()

for line in lines[1:]:
    cols = line.split(',')
    points.append(DataPoint(*[float(c) for c in cols]))

vi: List[Velocity] = [
    Velocity(
        dx=p.speed * sin(radians(p.heading)),
        dy=p.speed * cos(radians(p.heading))
    ) for p in points
]

@dataclass
class WindSolve:
    airspeed: float
    wx: float
    wy: float


def err_total(vi: List[Velocity], s: WindSolve) -> float:
    err = 0
    for v in vi:
        err += pow(s.airspeed - sqrt(pow(v.dy - s.wy, 2) + pow(v.dx - s.wx, 2)), 2)
    return err


def err_f(vi: List[Velocity]) -> Callable[[float], float]:
    return lambda x: err_total(vi, WindSolve(airspeed=x[0], wx=x[1], wy=x[2]))


N_WINDOW = 60
N_STEP = 1
N_STEPS = 5 * 60
START_AT = 5 * 60


@dataclass
class Frame:
    t: timedelta
    vi: List[Velocity]
    s: WindSolve
    lat: float
    lng: float
    alt: float
    ground_speed: float


print("Solving for wind...")
t0 = datetime.now()
frames: List[Frame] = []
prev_x = np.array([10, 0, 0])
for i0 in range(START_AT, START_AT + N_STEPS * N_STEP, N_STEP):
    i1 = i0 + N_WINDOW
    result = minimize(err_f(vi[i0:i1]), prev_x, method='Nelder-Mead')
    prev_x = result.x
    p = points[i1 - 1]
    frames.append(Frame(
        t=timedelta(seconds=i0 + N_WINDOW),
        vi=vi[i0:i0+N_WINDOW],
        s=WindSolve(airspeed=result.x[0], wx=result.x[1], wy=result.x[2]),
        lat=p.lat,
        lng=p.lng,
        alt=p.alt,
        ground_speed=p.speed,
    ))
print(f"Solved {len(frames)} wind points in {(datetime.now() - t0).total_seconds():.1f}s")

t = [frame.t.total_seconds() for frame in frames]
ws = [sqrt(pow(frame.s.wx, 2) + pow(frame.s.wy, 2)) for frame in frames]
wd = [degrees(atan2(frame.s.wx, frame.s.wy)) for frame in frames]
alt = [frame.alt for frame in frames]
gs = [frame.ground_speed for frame in frames]

# Create a figure and axis
fig, ax1 = plt.subplots(figsize=(10, 6))

# Scatter plot: Wind speed vs time, colored by wind direction
scatter_ws = ax1.scatter(t, ws, c=wd, cmap='hsv', s=50)
ax1.set_xlabel('Time (t)')
ax1.set_ylabel('Wind Speed (ws)', color='blue')
ax1.tick_params(axis='y', labelcolor='blue')
ax1.set_title('Wind Speed and Altitude vs Time')

# Add a colorbar for wind direction
cbar_ws = fig.colorbar(scatter_ws, ax=ax1, orientation='vertical', label='Wind Direction (wd) [°]')

# Create a second y-axis for altitude
ax2 = ax1.twinx()
ax2.plot(t, alt, color='green', linewidth=2, label='Altitude')
ax2.set_ylabel('Altitude (alt)', color='green')
ax2.tick_params(axis='y', labelcolor='green')

# Create a third y-axis for ground speed
ax3 = ax1.twinx()
ax3.plot(t, gs, color='red', linewidth=2, label='Ground speed')
ax3.set_ylabel('Ground speed', color='red')
ax3.tick_params(axis='y', labelcolor='red')


# Add a legend for the altitude line
ax2.legend(loc='upper right')

# Adjust layout and show the plot
plt.tight_layout()
plt.show()

# =============== Animation ===================

# Set up the figure and axis
fig, ax = plt.subplots()
ax.set_xlim(-15, 15)
ax.set_ylim(-15, 15)
ax.set_aspect('equal')

# Initialize scatter plot and circle
scatter = ax.scatter([], [], color='blue', label='Data points')
circle = plt.Circle((0, 0), 0.5, color='red', fill=False, label='Circle')
center = ax.scatter([], [], color='red', label='Wind')
center_ref = ax.scatter([0], [0], color='black')
ax.add_artist(circle)

# Initialization function
def init():
    scatter.set_offsets(np.empty((0, 2)))  # Clear scatter points
    circle.set_center((0, 0))  # Reset circle position
    center.set_offsets(np.empty((0, 2)))
    ax.set_title("Time: ")
    return scatter, circle, center, ax

# Animation function
def update(f):
    frame: Frame = frames[f]

    # Update scatter data
    x_data = [v.dx for v in frame.vi]
    y_data = [v.dy for v in frame.vi]
    scatter.set_offsets(np.c_[x_data, y_data])
    center.set_offsets(np.c_[[frame.s.wx], [frame.s.wy]])

    # Update circle position and radius
    circle.set_center((frame.s.wx, frame.s.wy))
    circle.set_radius(frame.s.airspeed)

    ax.set_title(f"Time: {frame.t}")

    return scatter, circle, center, ax

# Create the animation
ani = FuncAnimation(fig, update, frames=len(frames), init_func=init, blit=False, interval=50)

# Add labels, title, and legend
plt.xlabel('East')
plt.ylabel('North')
plt.title('Velocity')
plt.legend()

# Display the animation
plt.show()
