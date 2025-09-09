# This script opens a database and queries a variable for all time slices. The user specifies
# the database and variable, and the time, maximum value, and coordinate where the maximum value
# was found is printed to a file.
#
# Run this with
#
# Serial: visit -cli -nowin -s GetMax.py -database "..." -variable "..." -output "..."
# Parallel: visit -nn <num_nodes> -np <procs_per_node> -cli -nowin -s GetMax.py  -database "..." -variable "..." -output "..."
#
# If using slurm, one can allocate with
#
# > salloc --account=nnXXXXk --time=00:30:00 --nodes=4 --qos=devel
#

import argparse
import os

FileFormat = "{: <20} {: <20} {: <20} {: <20} {: <20}\n"

# # Input argument parser.
parser = argparse.ArgumentParser()
parser.add_argument('-database',    type=str, help="Absolute path to database. Use e.g. with 'plt/simulation2d.step*.hdf5 database' if opening multiple files", required=True)
parser.add_argument('-variable',    type=str, help="Which variable to query", required=True)
parser.add_argument('-output_file', type=str, help="Output file", default="output.dat", required=False)
parser.add_argument('-every_nth', type=int, help="Every nth step", default=1, required=False)

args,unknown = parser.parse_known_args()

# Open database.
OpenDatabase(args.database)

AddPlot("Curve", "operators/Lineout/{}".format(args.variable), 1, 1)
LineoutAtts = LineoutAttributes()
LineoutAtts.point1 = (0, 0.003, 0)
LineoutAtts.point2 = (0.012, 0.003, 0)
LineoutAtts.interactive = 0
LineoutAtts.ignoreGlobal = 0
LineoutAtts.samplingOn = 0
LineoutAtts.numberOfSamplePoints = 50
LineoutAtts.reflineLabels = 0
SetOperatorOptions(LineoutAtts, 0, 1)
DrawPlots()

n_time_steps = TimeSliderGetNStates()

# TimeSliderSetState(0)

if os.path.exists(args.output_file):
    os.remove(args.output_file)
    print(f"File '{args.output_file}' deleted successfully.")
else:
    print(f"File '{args.output_file}' does not exist.")

print("writing new file")
with open(args.output_file, 'w') as f:        
    f.write('time,length,{}\n'.format(args.variable))
for i in range(0, n_time_steps, args.every_nth):
   
    SetTimeSliderState(i)
    time = GetQueryOutputValue(Query("Time"))
    DrawPlots()
    vals = GetPlotInformation()["Curve"]
    # Extract x values (elements at even indices)
    x_list = list(vals[::2])
    # Extract y values (elements at odd indices)
    y_list = list(vals[1::2])
    data = [x_list, y_list]
   

    print("writing time step {} s".format(time))
    with open(args.output_file, 'a') as f:        
        for x, y in zip(x_list, y_list):        
            f.write(f"{time},{x},{y}\n")
    
DeleteAllPlots()
CloseDatabase(args.database)     
