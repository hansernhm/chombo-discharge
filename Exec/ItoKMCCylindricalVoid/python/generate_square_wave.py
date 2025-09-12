import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

def raised_cosine(t, t0, t1):
    """Smooth transition from 0 to 1 between t0 and t1 using raised cosine."""
    T = t1 - t0
    x = (t - t0) / T
    return 0.5 * (1 - np.cos(np.pi * x))

def generate_periodic_square_wave(t_start, t_end, dt, period, pulse_width, rise_time, amplitude=1.0, offset=0.0):
    """
    Generates a periodic smooth square wave using raised cosine ramps.

    Parameters:
    - t_start, t_end: signal start/end time (s)
    - dt: sampling interval (s)
    - period: repetition period of the waveform (s)
    - pulse_width: duration of each pulse (s)
    - rise_time: rise/fall time (s)
    - amplitude: peak amplitude of the pulse
    - offset: DC offset added to the waveform

    Returns:
    - DataFrame with time and pulse values
    """
    t = np.arange(t_start, t_end, dt)
    pulse = np.zeros_like(t)

    for i, ti in enumerate(t):
        # Determine where we are in the current period
        t_in_period = ti % period

        t_rise_start = 0
        t_rise_end = rise_time
        t_fall_start = pulse_width - rise_time
        t_fall_end = pulse_width

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

    # Add offset
    pulse += offset

    df = pd.DataFrame({'time': t, 'pulse': pulse})
    return df

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


# Parameters
t_start = 0
t_end = 4e-7           # 2 µs total duration
dt = 1e-11              # 1 ns sampling interval
period = 100e-9        # x ns period
rise_time = 1e-8    # 50 ns rise/fall time
pulse_width = 50e-9   # 300 ns pulse width
amplitude = 1       # Peak amplitude
offset = 0           # DC offset

# Generate and save
df_wave = generate_periodic_square_wave(t_start, t_end, dt, period, pulse_width, rise_time, amplitude, offset)
df_wave.to_csv("periodic_square_wave.dat", index=False, sep=' ')

# Plot
plt.plot(df_wave['time'].values * 1e6, df_wave['pulse'].values)
plt.xlabel("Time (µs)")
plt.ylabel("Amplitude")
plt.title("Periodic Smooth Square Wave with Offset")
plt.grid(True)
# plt.show()


# Compute derivative of the pulse
df_deriv = compute_derivative(df_wave, column='pulse')

# Merge with original waveform for comparison
df_wave_with_deriv = pd.concat([df_wave, df_deriv.drop(columns='time')], axis=1)

# Save to CSV
df_wave_with_deriv.to_csv("periodic_square_wave_with_derivative.dat", index=False)

# Plot both
plt.figure(figsize=(10, 5))
plt.plot(df_wave['time'].values * 1e6, df_wave['pulse'].values, label='Pulse')
plt.plot(df_wave['time'].values * 1e6, df_wave_with_deriv['dpulse/dt'].values, label='Derivative', linestyle='--')
plt.xlabel("Time (µs)")
plt.ylabel("Amplitude / Derivative")
plt.title("Pulse and Its Derivative")
plt.legend()
plt.grid(True)
plt.show()