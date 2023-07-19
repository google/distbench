# Distbench analysis script

The distbench analysis tool is a python script developed to transform the results from the native distbench proto format to different formats that can be used for compabality with existing analysis tools.

## How to use it

To use this script, numpy should be installed, one can do this on the linux terminal with the following command:

```bash
sudo apt install python3-numpy
```

The script generates a directory tree with summaries across all the clients and services.

to invoke the script use the follwing command line:

```bash
    ./results_conversion --output_directory=output_directory --input_file=name_of_the_file
```

The script also supports different formats for the output file, and options such as include or not the warmup rpcs and include or not a header at the beggining of every file.

## Flags
- **--output_directory**: path of the directory inside of which the directory tree will be created. (type=string)
- **--input_file**: path of the protobuf file that the script will read. (type=string)
- **--output_format**: Selects the format in which the summaries are gonna be. (type=string)
- **--supress_header**: by adding this flag, the summary files won't have a header. (type=bool)
- **--include_warmups**: by adding this flags, the summary files while include the warmup rpc's. (type=bool)


The formats the script supports are: 
- **default**: Summarizes the size of each rpc request and the latency.
- **statistics**: groups by size of rpc request and gives statistic info regarding the latencies of each group.
- **trace_context**: Groups by the first action iteration inside the trace_context field of the rpc and finds the longest latency for each group.

```bash
   ./results_conversion --output_directory=${DIRECTORY} --input_file=${INPUT_FILE}--output_format=statistics --supress_header
```