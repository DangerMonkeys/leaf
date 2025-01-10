from datetime import timedelta
from typing import List

from wind_estimation.sampling import compute_times
from wind_estimation.sensor_data import DataPoint, load_sensor_data
from wind_estimation.solve_v1 import solve_wind
from wind_estimation.visualization import visualize


points: List[DataPoint] = load_sensor_data("example1.csv")
frame_times = compute_times(
    start_time=timedelta(minutes=30),
    sample_period=timedelta(seconds=5),
    duration=timedelta(minutes=15))
frames = solve_wind(points, frame_times)
visualize(frames, 60)
