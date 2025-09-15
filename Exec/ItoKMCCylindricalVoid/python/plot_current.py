import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
from scipy.integrate import cumulative_trapezoid
import scipy.constants as constants
from scipy.fft import fft, fftfreq
from scipy.signal.windows import blackman
from scipy.signal.windows import hamming


def function_plot_and_fft(df, title='', plot_color='C0', nhead=0, ntail=0):
        
    fig, ax = plt.subplots(2)
    ntail = ntail  ## number of samples to cut at the beginning of recording
    df = df.drop(df.tail(ntail).index)  ## cut samples at beginning
    nhead = nhead  ## number of samples to cut at the end of recording
    df = df.drop(df.head(nhead).index)   ## Cut samples at end

    df.reset_index(inplace=True)  ## just a reindexing
    time = df.time.values
    current = df.current.values
    
    ax[0].plot(time, current, color=plot_color, linestyle='-', label='Current {}'.format(title))  ## TIME DOMAIN PLOT
    ax[0].set_ylabel('Current [A/m]')   ## SET Y LABEL
    ax[0].set_xlabel('Time [s]')  ## SET X LABEL
    ax[0].legend()  

    T = time[1]-time[0]  ## FIND TIME RESOLUTION IN RECODRING (NB ASSUMES UNIFORM SAMPLING)
    N = len(current)  ##  FIND LENGTH OF (TRUNCATED) RECODING
    w = blackman(N)  ## CALCULATE FFT WINDOW

    yf1 = fft(current * w)  ### CALCULATE FFT
    xf = fftfreq(N, T)[:N//2]  ## CALCULATE FFT FREQUENCY ARRAY
    ax[1].plot(xf, 20*np.log10( 2.0/N * np.abs(yf1[0:N//2])), color=plot_color, linestyle='-', label='FFT current {}'.format(title)) ## PLOT FFT
    ax[1].legend()
    plt.tight_layout() ### FORMATTING FOR SAVING
    ax[1].set_xlabel('Frequency [Hz]') ## SET X LABEL
    ax[1].set_ylabel('Amplitude [dB] \n (ref.level 0 dBm)') ## SET Y LABEL


if __name__ == '__main__':
    df = pd.read_csv('curve.txt')

    # group and collapse the data
    collapsed = df.groupby('time').agg({
                                        'length': list,
                                        'y-Electric field': list
                                        }).reset_index()

    # Convert lists to arrays for numerical operations
    collapsed['length'] = collapsed['length'].apply(np.array)
    collapsed['y-Electric field'] = collapsed['y-Electric field'].apply(np.array)

    # Compute the integral using the trapezoidal rule
    collapsed['integral_Efield'] = collapsed.apply(
        lambda row: np.trapz(row['y-Electric field'], row['length']),
        axis=1
    )

    # Assuming 'collapsed' is your DataFrame with 'time' and 'integral_Efield'
    collapsed['dEfield_dt'] = np.gradient(collapsed['integral_Efield'], collapsed['time'])

    # plt.plot(collapsed['length'].values[0], collapsed['y-Electric field'].values[0])
    # plt.plot(collapsed['length'].values[90])
    epsilon0 = 8.8541878128e-12  # F/m
    epsilon_r = 1.0              # relative permittivity (set appropriately)
    epsilon = epsilon0 * epsilon_r
    W = 3e-3  # mm, depth

    collapsed['current'] = epsilon * collapsed['dEfield_dt'] * W
    collapsed['Qtotal'] = np.trapz(collapsed['current'], collapsed['time'])

    # Compute cumulative charge over time
    collapsed['Q_charge'] = np.cumsum(
        np.concatenate([[0], np.diff(collapsed['time']) * collapsed['current'].iloc[:-1]])
    )

    #plt.plot(collapsed['time'].values, collapsed['current'].values)
    # plt.xlabel('Time [s]')
    # plt.ylabel('Amps [A]')
    
    df_voltage = pd.read_csv('periodic_square_wave.dat', sep=' ')
    print(df_voltage.keys())
    fig, ax1 = plt.subplots(figsize=(10, 5))
    level = 12.5e3
    ax1.plot(df_voltage['time'].values, df_voltage.pulse.values * level,color='tab:blue', label='Voltage')
    ax1.set_xlabel('Time (s)')
    
    ax1.set_ylabel('Voltage (V)', color='tab:blue')
    ax1.tick_params(axis='y', labelcolor='tab:blue')
    ax2 = ax1.twinx()
    ax2.plot(collapsed['time'].values, collapsed['current'].values, color='tab:red', linestyle='--', label='Current')
    ax2.set_ylabel('Current (A/m)', color='tab:red')
    ax2.tick_params(axis='y', labelcolor='tab:red')
    plt.grid(True)
    function_plot_and_fft(collapsed, ntail=350)
    
    plt.tight_layout()
    
    plt.show()
    
