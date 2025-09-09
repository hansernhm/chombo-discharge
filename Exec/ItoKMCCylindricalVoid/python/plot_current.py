import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
from scipy.integrate import cumulative_trapezoid
import scipy.constants as constants

df = pd.read_csv('curve.txt')
print(df)

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
plt.figure()


epsilon0 = 8.8541878128e-12  # F/m
epsilon_r = 1.0              # relative permittivity (set appropriately)
epsilon = epsilon0 * epsilon_r

collapsed['current'] = epsilon * collapsed['dEfield_dt']
collapsed['Qtotal'] = np.trapz(collapsed['current'], collapsed['time'])

print(collapsed)

plt.plot(collapsed['time'].values, collapsed['current'].values)
plt.show()
