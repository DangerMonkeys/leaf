from datetime import datetime, timedelta
from typing import List

from wind_estimation.sampling import compute_times
from wind_estimation.sensor_data import DataPoint, load_sensor_data
from wind_estimation.strategies.absolute_fit import solve_wind
from wind_estimation.visualization import visualize


SENSOR_DATA_FILENAME = "example1.csv"
ESTIMATION_START_TIME = timedelta(minutes=30)
ESTIMATION_PERIOD = timedelta(seconds=5)
ESTIMATION_DURATION = timedelta(minutes=15)
WINDOW_DURATION = timedelta(seconds=60)

print("Loading sensor data...")
t0 = datetime.now()
points: List[DataPoint] = load_sensor_data(SENSOR_DATA_FILENAME)
print(f"    Loaded {len(points)} data points in {(datetime.now() - t0).total_seconds():.1f}s")

frame_times = compute_times(
    start_time=ESTIMATION_START_TIME,
    sample_period=ESTIMATION_PERIOD,
    duration=ESTIMATION_DURATION)

print("Estimating wind...")
t0 = datetime.now()
frames = solve_wind(points, frame_times)
print(f"    Estimated {len(frames)} wind points in {(datetime.now() - t0).total_seconds():.1f}s")

visualize(frames, WINDOW_DURATION.total_seconds())
