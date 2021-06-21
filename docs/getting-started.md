# Distbench Getting Started

## Introduction

This document describes how to build Distbench and run a simple test.
For general information about Distbench, please consult the
[Distbench Overview](quick-overview.md) and/or the [README.md](../README.md).

## Getting started

### Dependencies

To build Distbench, you need to have Bazel and grpc\_cli installed.

Follow the instructions for your distribution at
<https://docs.bazel.build/versions/master/install.html> to install Bazel.

For grpc\_cli, follows the instruction at
<https://github.com/grpc/grpc/blob/master/doc/command_line_tool.md> to build
the grpc\_cli command line tool.

Quick instructions for GRPC:

```bash
sudo apt install cmake
git clone https://github.com/grpc/grpc.git
cd grpc
git submodule update --init
mkdir -p cmake/build
cd cmake/build
cmake -DgRPC_BUILD_TESTS=ON ../..
make grpc_cli
export PATH=`pwd`:$PATH
```

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

It should output a test report:
```
(...)
  }
  log_summary: "RPC latency summary:\nleaf_query: N: 36 min: 134248ns median:
  282243ns 90%: 1036470ns 99%: 1339838ns 99.9%: 1339838ns max:
  1339838ns\nroot_query: N: 12 min: 594520ns median: 962939ns 90%: 5803888ns
  99%: 6048966ns 99.9%: 6048966ns max: 6048966ns\n"
}
Rpc succeeded with OK status
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
