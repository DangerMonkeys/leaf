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


MIN_AIRSPEED = 5
MAX_AIRSPEED = 25
def err_total(vi: List[Velocity], s: WindSolve) -> float:
    err = 0
    for v in vi:
        err += pow(s.airspeed - sqrt(pow(v.dy - s.wy, 2) + pow(v.dx - s.wx, 2)), 2)
    if s.airspeed < MIN_AIRSPEED:
        err *= (MIN_AIRSPEED - s.airspeed + 1)
    if s.airspeed > MAX_AIRSPEED:
        err *= (s.airspeed - MAX_AIRSPEED + 1)
    return err


def err_f(vi: List[Velocity]) -> Callable[[float], float]:
    return lambda x: err_total(vi, WindSolve(airspeed=x[0], wx=x[1], wy=x[2]))


# Number of trailing samples to use to compute wind speed and direction
N_WINDOW = 60

# How many samples to move forward each animation frame / visualization time step
N_STEP = 5

# Number of animation frames / visualization time steps to compute and show
N_STEPS = int(15 * 60 / N_STEP)

# Tim
START_AT = 30 * 60


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


print("Solving for wind...")
t0 = datetime.now()
frames: List[Frame] = []
prev_x = np.array([10, 0, 0])
for i1 in range(START_AT + 1, START_AT + N_STEPS * N_STEP + 2, N_STEP):
    i0 = i1 - N_WINDOW + 1
    result = minimize(err_f(vi[i0:i1]), np.array([10, 0, 0]), method='Nelder-Mead')
    prev_x = result.x
    p = points[i1]
    s = WindSolve(airspeed=result.x[0], wx=result.x[1], wy=result.x[2])
    frames.append(Frame(
        t=timedelta(seconds=i1),
        vi=vi[i0:i1],
        s=s,
        lat=p.lat,
        lng=p.lng,
        alt=p.alt,
        wind_speed=sqrt(pow(s.wx, 2) + pow(s.wy, 2)),
        wind_direction=degrees(atan2(s.wx, s.wy)),
        ground_speed=p.speed,
        track=p.heading,
    ))
print(f"Solved {len(frames)} wind points in {(datetime.now() - t0).total_seconds():.1f}s")

# Set up the figure and axis
fig, axs = plt.subplots(2, 1)

t = [frame.t.total_seconds() for frame in frames]
ws = [frame.wind_speed for frame in frames]
wd = [frame.wind_direction for frame in frames]
alt = [frame.alt for frame in frames]
gs = [frame.ground_speed for frame in frames]
t_track = t
track = [frame.track for frame in frames]

i = 1
while i < len(track):
    if (track[i - 1] > 330 and track[i] < 30) or (track[i - 1] < 30 and track[i] > 330):
        track = track[0:i] + [float('nan')] + track[i:]
        t_track = t_track[0:i] + [0.5 * (t_track[i - 1] + t_track[i])] + t_track[i:]
    i += 1

# Scatter plot: Wind speed vs time, colored by wind direction
scatter_ws = axs[1].scatter(t, ws, c=wd, cmap='hsv', s=50, vmin=-180, vmax=180)
axs[1].set_xlabel('Time (t)')
axs[1].set_ylabel('Wind Speed (ws)', color='blue')
axs[1].tick_params(axis='y', labelcolor='blue')
ws_now, = axs[1].plot([], [], color='k', linewidth=2)
ws_earlier, = axs[1].plot([], [], color='gray', linewidth=1)

# Add a colorbar for wind direction
cbar_ws = fig.colorbar(scatter_ws, ax=axs[1], orientation='vertical', label='Wind Direction (wd) [°]')

# Create a second y-axis for altitude
ax2 = axs[1].twinx()
ax2.plot(t, alt, color='green', linewidth=2, label='Altitude')
ax2.set_ylabel('Altitude (alt)', color='green')
ax2.tick_params(axis='y', labelcolor='green')

# Create a third y-axis for ground speed
ax3 = axs[1].twinx()
ax3.plot(t, gs, color='red', linewidth=2, label='Ground speed')
ax3.set_ylabel('Ground speed', color='red')
ax3.tick_params(axis='y', labelcolor='red')

# Create a fourth y-axis for track
ax4 = axs[1].twinx()
ax4.plot(t_track, track, color='orange', linewidth=2, label='Track')
ax4.set_ylabel('Track', color='orange')
ax4.tick_params(axis='y', labelcolor='orange')
ax4.set_ylim(0, 360)

# Add a legend for the altitude line
ax2.legend(loc='upper right')

axs[0].set_xlim(-15, 15)
axs[0].set_ylim(-15, 15)
axs[0].set_aspect('equal')
axs[0].set_title('Velocity')

# Initialize scatter plot and circle
scatter = axs[0].scatter([], [], color='blue', label='Data points')
circle = plt.Circle((0, 0), 0.5, color='red', fill=False, label='Circle')
center = axs[0].scatter([], [], color='red', label='Wind')
center_ref = axs[0].scatter([0], [0], color='black')
axs[0].add_artist(circle)

# Initialization function
def init():
    scatter.set_offsets(np.empty((0, 2)))  # Clear scatter points
    circle.set_center((0, 0))  # Reset circle position
    center.set_offsets(np.empty((0, 2)))
    axs[1].set_title("Time: ")
    ws_now.set_data([], [])
    ws_earlier.set_data([], [])
    return scatter, circle, center, axs[1], ws_now, ws_earlier

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

    ws_now.set_data([frame.t.total_seconds()] * 2, [min(ws), max(ws)])
    ws_earlier.set_data([frame.t.total_seconds() - N_WINDOW] * 2, [min(ws), max(ws)])

    axs[1].set_title(f"Time: {frame.t}")

    return scatter, circle, center, axs[1], ws_now, ws_earlier

# Create the animation
ani = FuncAnimation(fig, update, frames=len(frames), init_func=init, blit=False, interval=50)

# Add labels, title, and legend
plt.legend()

# Display the animation
plt.tight_layout()
plt.show()
