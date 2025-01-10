from typing import List

from wind_estimation.sensor_data import DataPoint, load_sensor_data
from wind_estimation.solve_v1 import solve_wind
from wind_estimation.visualization import visualize


points: List[DataPoint] = load_sensor_data("example1.csv")
frames = solve_wind(points)
visualize(frames, 60)
