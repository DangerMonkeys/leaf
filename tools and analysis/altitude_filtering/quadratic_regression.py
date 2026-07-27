from typing import Tuple

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import numpy as np
from numpy.typing import NDArray


REGRESSION_WINDOW_S = 1

def load_baro_data(csv_file_name: str) -> NDArray[np.float32]:
    """Load barometer data from the specified CSV file.

    Args:
        csv_file_name: Path to CSV containing a time column and a baro value column, after a header row.

    Returns: Parsed data for all N value rows in CSV as an Nx2 array.
    """
    with open(csv_file_name, "r") as f:
        lines = f.readlines()

    rows = []
    for r, line in enumerate(lines[1:]):
        cols = [float(v) for v in line.split(',')]
        rows.append(cols)

    return np.array(rows)


def quadratic_regression(ti: NDArray[np.float32], yi: NDArray[np.float32]) -> Tuple[float, float, float]:
    # Center data around zero for numeric stability
    mu_t = np.mean(ti)
    mu_y = np.mean(yi)
    ti = ti - mu_t
    yi = yi - mu_y

    # Compute sums
    n = ti.size
    Eti = np.sum(ti)
    Eti2 = np.sum(ti * ti)
    Eti3 = np.sum(ti * ti * ti)
    Eti4 = np.sum(ti * ti * ti * ti)
    Eti2yi = np.sum(ti * ti * yi)
    Etiyi = np.sum(ti * yi)
    Eyi = np.sum(yi)

    # Calculate coefficients
    a_numerator = Eti * Eti * Eti2yi + Eti2 * Eti2 * Eyi - Eti * (Eti2 * Etiyi + Eti3 * Eyi) - Eti2 * Eti2yi * n + Eti3 * Etiyi * n
    a_denominator = Eti2 * Eti2 * Eti2 + Eti * Eti * Eti4 + Eti3 * Eti3 * n - Eti2 * (2 * Eti * Eti3 + Eti4 * n)
    a = a_numerator / a_denominator

    b_numerator = -a * Eti * Eti2 + Eti * Eyi + a * Eti3 * n - Etiyi * n
    b_denominator = Eti * Eti - Eti2 * n
    b = b_numerator / b_denominator

    c = (-b * Eti - a * Eti2 + Eyi) / n

    # Return coefficients after removing mu_t and _mu_y offsets
    return a, b - 2 * a * mu_t, c + a * mu_t * mu_t - b * mu_t + mu_y

def visualize(ti: NDArray[np.float32], yi: NDArray[np.float32], fit_times: NDArray[np.float32]) -> None:
    # Set up the figure and axis
    fig, axs = plt.subplots(1, 1)

    # Scatter plot: Barometer vs time
    scatter_baro = axs.scatter(ti, yi, s=2)
    axs.set_xlabel('Time (seconds)')
    axs.set_ylabel('Barometer (mBar)', color='blue')
    axs.tick_params(axis='y', labelcolor='blue')

    # Calculate fit and extrapolation for every time point
    rawfit_t = ti[2:]
    rawfit_y = np.zeros(rawfit_t.shape, dtype=float)
    rawfit_v = np.zeros(rawfit_t.shape, dtype=float)
    for i in range(ti.size - 2):
        rawfit_ti = ti[0:i+3]
        rawfit_yi = yi[0:i+3]
        mask = rawfit_ti >= rawfit_ti[-1] - REGRESSION_WINDOW_S
        rawfit_ti = rawfit_ti[mask]
        rawfit_yi = rawfit_yi[mask]
        a, b, c = quadratic_regression(rawfit_ti, rawfit_yi)
        rawfit_y[i] = a * rawfit_t[i] * rawfit_t[i] + b * rawfit_t[i] + c
        rawfit_v[i] = 2 * a * rawfit_t[i] + b
    scatter_rawfit = axs.plot(rawfit_t, rawfit_y, color='blue')
    ax2 = axs.twinx()
    ax2.plot(rawfit_t, rawfit_v, color='green', linewidth=1)
    ax2.set_ylabel('Vertical speed (mBar/s)', color='green')
    ax2.tick_params(axis='y', labelcolor='green')

    # Sliding fit visualizations
    scatter_fit = axs.scatter(ti, yi, s=4, color='r')
    fit_now, = axs.plot([], [], color='r', linewidth=2)

    # Initialization function
    def init():
        fit_now.set_data([], [])
        scatter_fit.set_offsets(np.empty((0, 2)))
        return scatter_fit, fit_now

    # Animation function
    def update(f):
        mask = np.logical_and(ti >= fit_times[f] - REGRESSION_WINDOW_S, ti <= fit_times[f])

        scatter_fit.set_offsets(np.c_[ti[mask], yi[mask]])

        a, b, c = quadratic_regression(ti[mask], yi[mask])
        t = np.linspace(fit_times[f] - REGRESSION_WINDOW_S, fit_times[f], int(REGRESSION_WINDOW_S / 0.1))
        y = a * t * t + b * t + c
        fit_now.set_data(t, y)

        return scatter_fit, fit_now

    # Create the animation
    ani = FuncAnimation(fig, update, frames=fit_times.size, init_func=init, blit=False, interval=50)

    # Display the animation
    plt.tight_layout()
    plt.show()


data = load_baro_data("baro20hz_1.csv")
ti = data[:, 0]
yi = data[:, 1]
fit_times = np.arange(np.min(ti) + REGRESSION_WINDOW_S, np.max(ti), 0.1)
visualize(ti, yi, fit_times)
