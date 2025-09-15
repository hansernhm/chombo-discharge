import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

def raised_cosine(t, t0, t1):
    """Smooth transition from 0 to 1 between t0 and t1 using raised cosine."""
    T = t1 - t0
    x = (t - t0) / T
    return 0.5 * (1 - np.cos(np.pi * x))

def generate_periodic_square_wave(t_start, t_end, dt, period, duty_cycle, rise_time,
                                   amplitude=1.0, offset=0.0, phase_shift_ms=0.0):
    """
    Generates a periodic smooth square wave using raised cosine ramps with duty cycle and phase shift.

    Parameters:
    - t_start, t_end: signal start/end time (s)
    - dt: sampling interval (s)
    - period: repetition period of the waveform (s)
    - duty_cycle: fraction of period the pulse is high (0 to 1)
    - rise_time: rise/fall time (s)
    - amplitude: peak amplitude of the pulse
    - offset: DC offset added to the waveform
    - phase_shift_ms: phase shift in milliseconds

    Returns:
    - DataFrame with time and pulse values
    """
    t = np.arange(t_start, t_end, dt)
    pulse = np.zeros_like(t)

    # Convert phase shift to seconds
    phase_shift = phase_shift_ms

    # Compute pulse width from duty cycle
    pulse_width = period * duty_cycle

    for i, ti in enumerate(t):
        # Apply phase shift to time within each period
        t_in_period = (ti - phase_shift) % period

        t_rise_start = 0
        t_rise_end = rise_time
        t_fall_start = pulse_width 
        t_fall_end = pulse_width + rise_time

        if t_in_period < t_rise_start:
            pulse[i] = 0
        elif t_rise_start <= t_in_period < t_rise_end:
            pulse[i] = amplitude * raised_cosine(t_in_period, t_rise_start, t_rise_end)
        elif t_rise_end <= t_in_period < t_fall_start:
            pulse[i] = amplitude
        elif t_fall_start <= t_in_period < t_fall_end:
            pulse[i] = amplitude * (1 - raised_cosine(t_in_period, t_fall_start, t_fall_end))
        else:
            pulse[i] = 0

    pulse += offset
    return pd.DataFrame({'time': t, 'pulse': pulse})






def compute_derivative(df, column='pulse'):
    """
    Computes the numerical derivative of a column in a DataFrame.

    Parameters:
    - df: DataFrame with 'time' and signal column
    - column: name of the column to differentiate

    Returns:
    - DataFrame with 'time' and 'derivative' columns
    """
    time = df['time'].values
    signal = df[column].values
    derivative = np.gradient(signal, time)

    df_deriv = pd.DataFrame({
        'time': time,
        f'd{column}/dt': derivative
    })
    return df_deriv


def plot_pulse_and_derivative(df, pulse_col='pulse', time_col='time'):
    """
    Plots the pulse and its derivative on separate y-axes.

    Parameters:
    - df: DataFrame containing time, pulse, and derivative columns
    - pulse_col: name of the pulse column
    - time_col: name of the time column
    """
    time = df[time_col].values * 1e6  # Convert to microseconds for readability
    pulse = df[pulse_col].values
    derivative = np.gradient(pulse, df[time_col].values)

    fig, ax1 = plt.subplots(figsize=(10, 5))

    # Plot pulse on left y-axis
    ax1.plot(time, pulse, color='tab:blue', label='Pulse')
    ax1.set_xlabel('Time (µs)')
    ax1.set_ylabel('Amplitude', color='tab:blue')
    ax1.tick_params(axis='y', labelcolor='tab:blue')

    # Create second y-axis for derivative
    ax2 = ax1.twinx()
    ax2.plot(time, derivative, color='tab:red', linestyle='--', label='Derivative')
    ax2.set_ylabel('dPulse/dt', color='tab:red')
    ax2.tick_params(axis='y', labelcolor='tab:red')

    # Title and grid
    plt.title('Pulse and Its Derivative')
    fig.tight_layout()
    plt.grid(True)
    plt.show()

# Parameters
t_start = 0
t_end = 1.1e-7           # 2 µs total duration
dt = 1e-12              # 1 ns sampling interval
period = 60e-9        # x ns period
duty_cycle = 0.5
rise_time = 1e-8    #  ns rise/fall time
amplitude = 1       # Peak amplitude
offset = 0           # DC offset
phase_shift = -1.001e-8

# Generate and save
df_wave = generate_periodic_square_wave(t_start, 
                                        t_end, 
                                        dt, 
                                        period, 
                                        duty_cycle,
                                        rise_time, 
                                        amplitude, 
                                        offset,
                                        phase_shift)
df_wave.to_csv("periodic_square_wave.dat", index=False, sep=' ')

# Plot
plot_pulse_and_derivative(df_wave)