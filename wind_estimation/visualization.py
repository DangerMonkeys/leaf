import matplotlib.pyplot as plt
import numpy as np
from matplotlib.animation import FuncAnimation
from typing import List, Optional

from .estimation import Observation


def visualize(frames: List[Observation], window_duration_s: Optional[float] = None) -> None:
    # Set up the figure and axis
    fig, axs = plt.subplots(2, 1)

    # Extract data by field for convenience
    t = [frame.t.total_seconds() / 60 for frame in frames]
    ws = [frame.solve.wind.speed for frame in frames]
    wd = [frame.solve.wind.direction for frame in frames]
    alt = [frame.alt for frame in frames]
    gs = [frame.ground_speed for frame in frames]
    track_angle = [frame.track_angle for frame in frames]

    # Separate track wrap-arounds with nan so they don't plot across the graph
    t_track = t
    i = 1
    while i < len(track_angle):
        if (track_angle[i - 1] > 330 and track_angle[i] < 30) or (track_angle[i - 1] < 30 and track_angle[i] > 330):
            track_angle = track_angle[0:i] + [float('nan')] + track_angle[i:]
            t_track = t_track[0:i] + [0.5 * (t_track[i - 1] + t_track[i])] + t_track[i:]
        i += 1

    # Scatter plot: Wind speed vs time, colored by wind direction
    scatter_ws = axs[1].scatter(t, ws, c=wd, cmap='hsv', s=50, vmin=0, vmax=360)
    axs[1].set_xlabel('Time (minutes)')
    axs[1].set_ylabel('Wind Speed (m/s)', color='blue')
    axs[1].tick_params(axis='y', labelcolor='blue')
    ws_now, = axs[1].plot([], [], color='k', linewidth=2)
    ws_earlier, = axs[1].plot([], [], color='gray', linewidth=1)
    axs[1].set_ylim(0, 5)

    # Add a colorbar for wind direction
    cbar_ws = fig.colorbar(scatter_ws, ax=axs[1], orientation='horizontal', label='Wind Direction (°)')

    # Create a second y-axis for altitude
    ax2 = axs[1].twinx()
    ax2.plot(t, alt, color='green', linewidth=2)
    ax2.set_ylabel('Altitude (m)', color='green')
    ax2.tick_params(axis='y', labelcolor='green')

    # Create a third y-axis for ground speed
    ax3 = axs[1].twinx()
    ax3.plot(t, gs, color='blue', linewidth=1)
    ax3.set_ylabel('\nGround speed (m/s)', color='blue')
    ax3.tick_params(axis='y', labelcolor='blue')

    # Create a fourth y-axis for track
    ax4 = axs[1].twinx()
    ax4.plot(t_track, track_angle, color='purple', linewidth=1)
    ax4.set_ylabel('\n\nTrack (°)', color='purple')
    ax4.tick_params(axis='y', labelcolor='purple')
    ax4.set_ylim(0, 360)

    # Set up sampling and wind estimate figure
    axs[0].set_xlim(-15, 15)
    axs[0].set_ylim(-15, 15)
    axs[0].set_aspect('equal')
    axs[0].set_title('Velocity')
    scatter = axs[0].scatter([], [], color='blue', label='Data points')
    circle = plt.Circle((0, 0), 0.5, color='red', fill=False, label='Circle')
    circle_center = axs[0].scatter([], [], color='red', label='Wind')
    center_ref = axs[0].scatter([0], [0], color='black')
    axs[0].add_artist(circle)

    # Initialization function
    def init():
        scatter.set_offsets(np.empty((0, 2)))  # Clear scatter points
        circle.set_center((0, 0))  # Reset circle position
        circle_center.set_offsets(np.empty((0, 2)))
        axs[1].set_title("Time: ")
        ws_now.set_data([], [])
        ws_earlier.set_data([], [])
        return scatter, circle, circle_center, axs[1], ws_now, ws_earlier

    # Animation function
    def update(f):
        frame: Observation = frames[f]

        # Update velocity samples scatter data
        x_data = [p.ground_track.dx for p in frame.points]
        y_data = [p.ground_track.dy for p in frame.points]
        scatter.set_offsets(np.c_[x_data, y_data])

        # Update wind speed estimate circle position and radius
        circle_center.set_offsets(np.c_[[frame.solve.wind.dx], [frame.solve.wind.dy]])
        circle.set_center((frame.solve.wind.dx, frame.solve.wind.dy))
        circle.set_radius(frame.solve.airspeed)

        # Update "now" indicator
        ws_now.set_data([frame.t.total_seconds() / 60] * 2, [min(ws), max(ws)])

        # Update "beginning of window" indicator
        if window_duration_s is not None:
            ws_earlier.set_data([(frame.t.total_seconds() - window_duration_s) / 60] * 2, [min(ws), max(ws)])

        axs[1].set_title(f"Time: {frame.t}")

        return scatter, circle, circle_center, axs[1], ws_now, ws_earlier

    # Create the animation
    ani = FuncAnimation(fig, update, frames=len(frames), init_func=init, blit=False, interval=50)

    # Add labels, title, and legend
    plt.legend()

    # Display the animation
    plt.tight_layout()
    plt.show()
