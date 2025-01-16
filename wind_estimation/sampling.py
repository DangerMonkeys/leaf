from datetime import timedelta
from typing import List


def compute_times(start_time: timedelta, sample_period: timedelta, duration: timedelta) -> List[timedelta]:
    times: List[timedelta] = []
    t = start_time
    while t - start_time <= duration:
        times.append(t)
        t += sample_period
    return times
