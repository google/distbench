# Distbench Overview

## Introduction

Distbench is a tool for synthesizing a variety of network traffic patterns used
in distributed systems, and evaluating their performance across multiple
networking stacks.

A Distbench experiment consists of a single controller process and a set of
worker processes. The controller initiates a traffic pattern among the set of
workers, monitors traffic and generates a performance report for the RPCs. The
traffic pattern, specified spatially and temporally and the RPC stack are all
configurable by the controller. A block diagram is shown below.

![image](https://user-images.githubusercontent.com/22774907/122141907-2cfe0200-ce03-11eb-8c43-ce679d9639ac.png)

## Why Use Distbench ?

Use Distbench to:

- Identify system bottlenecks for various common distributed computing tasks,
- Compare RPC stack performance for various common traffic patterns,
- Evaluate the impact of system configurations, application threading models and
  kernel configurations on large, scale-out applications.

## Use Model Overview

- DistBench is itself a distributed system, consisting of a single controller
  process, and multiple worker processes.
- To startup an instance of DistBench, the controller process only needs to be
  told which port to start on, and the worker processes only need to be told the
  host:port of the controller.
- All actual work is triggered by RPCs sent to the controller which contain the
  test config in their request messages, and the test results are placed in the
  RPC response messages.
- Note that there is no need to change command line flags or restart binaries to
  run additional tests.

The following image provides more details.

![image](https://user-images.githubusercontent.com/22774907/122157754-42355980-ce20-11eb-8f1d-e4173d4ed4bc.png)

The main steps in a Distbench experiment are:

- User starts the DistBench controller and the Workers.
- The controller receives the test description from the User RPCs and reports
  the test results in the response.
  - A complete performance report is generated so that we can postprocess a
    variety of metrics after the test
- Application drivers and protocol drivers are started on-demand in response to
  User RPCs, and are terminated when a test ends.
- All of the synthesized application traffic is sent between the Protocol Driver
  instances.

Note that there is a 1:1 correspondence between application drivers and protocol
drivers, but each host/worker can have multiple or zero applications.

### Application Driver:

The Application Driver:
- Runs the application logic described in the IR.
- Allows DistBench to synthesize many different traffic patterns.
- Collects and publishes performance statistics.

### Protocol Driver Abstraction

The Protocol Driver:
- Converts abstract traffic to real RPC stack
- Allows a single DistBench binary to test a given traffic pattern across
  multiple RPC stacks.
- New RPC stacks can be easily added (~200 lines of code), thus avoiding the
  need to have N\*K benchmarks for N different traffic patterns and K different
  RPC stacks, work needed is now of O(N+K)...

The details of the Intermediate Representation (IR) of traffic patterns and the
spatial distribution are provided in the
[Distbench Traffic Pattern - Format](distbench-test-format.md)

## Other documentation

- [Distbench README](../README.md)
- [Distbench Getting Started](getting-started.md)
- [Distbench Workloads](../workloads/README.md)
- [Distbench Traffic Pattern - Format](distbench-test-format.md)

