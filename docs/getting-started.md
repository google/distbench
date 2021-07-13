# Distbench Getting Started

## Introduction

This document describes how to build Distbench and run a simple test.
For general information about Distbench, please consult the
[Distbench Overview](quick-overview.md) and/or the [README.md](../README.md).

## Getting started

### Dependencies

To build Distbench, you need to have Bazel installed.

Follow the instructions for your distribution at
<https://docs.bazel.build/versions/master/install.html> to install Bazel.

### Building Distbench

Once Bazel is installed, you can build Distbench with the following command:
```bash
bazel build :all
```

## Running unit tests

You can run the unit tests included with the following command:

```bash
bazel test :all
```

## Testing with ./simple\_test.sh

You can run a simple Distbench test on localhost using `simple_test.sh` which
will run a simple search-like pattern with all the services (load\_balancer,
root, leaf\*3) running on a single host.

You can either use the script `start_distbench_localhost.sh`,
to start a test\_sequencer and a node\_manager on localhost, or do it
manually as follows:
```bash
bazel run :distbench -- test_sequencer &
bazel run :distbench -- node_manager --test_sequencer=localhost:10000 --port=9999 &
```

Once distbench is running; run the traffic pattern:
```bash
./simple_test.sh
```

It should output the summary of test result:
```
RPC latency summary:
  leaf_query: N: 36 min: 474609ns median: 886503ns 90%: 1411603ns 99%: 1713296ns 99.9%: 1713296ns max: 1713296ns
  root_query: N: 12 min: 1191994ns median: 1817132ns 90%: 6142479ns 99%: 6469017ns 99.9%: 6469017ns max: 6469017ns
Communication summary:
  load_balancer/0 -> root/0: RPCs: 12 (0.01 kQPS) Request: 0.0 MiB/s Response: 0.0 MiB/s
  root/0 -> leaf/0: RPCs: 12 (0.01 kQPS) Request: 0.0 MiB/s Response: 0.0 MiB/s
  root/0 -> leaf/1: RPCs: 12 (0.01 kQPS) Request: 0.0 MiB/s Response: 0.0 MiB/s
  root/0 -> leaf/2: RPCs: 12 (0.01 kQPS) Request: 0.0 MiB/s Response: 0.0 MiB/s
Instance summary:
  leaf/0: Tx: 0.0 MiB/s, Rx:0.0 MiB/s
  leaf/1: Tx: 0.0 MiB/s, Rx:0.0 MiB/s
  leaf/2: Tx: 0.0 MiB/s, Rx:0.0 MiB/s
  load_balancer/0: Tx: 0.0 MiB/s, Rx:0.0 MiB/s
  root/0: Tx: 0.0 MiB/s, Rx:0.0 MiB/s
Global summary:
  Total time: 2.099s
  Total Tx: 0 MiB (0.0 MiB/s), Total Nb RPCs: 48 (0.02 kQPS)
```

## Running with debug enabled

To compile and run with debugging enabled:

```bash
bazel run --compilation_mode=dbg :distbench -- test_sequencer &
bazel run --compilation_mode=dbg :distbench -- node_manager --test_sequencer=localhost:10000 --port=9999 &
./simple_test.sh
```

## Distbench Workloads

Once `simple_test.sh` is running, please look at the
[Distbench Workloads](workloads/README.md) for more workloads to run.
