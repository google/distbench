This script is a script to analyze and plot the results obtained from running the cp_node traffic patterns in cp_node and distbench.

The script takes as flags:

## Flags
- **--directory** where the scriopt is gonna search for the files to analyze and where the plots will be saved inside a directory called reports

- **--workload** takes the workload that was used for the experiments

- **--gbps** Takes the GBPS used in the experiments

These two las flags are only used for naming purposes

## Usage:
'''bash
    ./cp_node_analysis.py [--flags=value]
'''