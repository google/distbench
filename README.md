# Distbench README

## Introduction

Distbench is a tool for synthesizing a variety of network traffic patterns used
in distributed systems, and evaluating their performance across multiple
networking stacks.

A Distbench experiment consists of a single controller process and a set of
worker processes. The controller initiates a traffic pattern among the set of
workers, monitors traffic and generates a performance report for the RPCs. The
traffic pattern, specified spatially and temporally and the RPC stack are all
configurable by the controller.

## Why Use Distbench ?

Use Distbench to:

- Identify system bottlenecks for various common distributed computing tasks,
- Compare RPC stack performance for various common traffic patterns,
- Evaluate the impact of system configurations, application threading models and
  kernel configurations on large, scale-out applications.

## Included documentation

- [Distbench Overview](docs/quick-overview.md): Provides an overview of
  Distbench, its model and design.
- [Distbench Getting Started](docs/getting-started.md): Provides information on
  how to compile and run Distbench for the first time.
- [Distbench Workloads](workloads/README.md): Describes the workloads included.
- [Distbench Traffic Pattern - Format](docs/distbench-test-format.md): Defined
- [Distbench Command Line Arguments](docs/command-line.md): Overview
  of the Distbench command line arguments.
- [Distbench Test Builder](test_builder/README.md): Tool to build and execute
  parametric distbench test sequences.
- [Distbench FAQs](docs/faq.md)
- [Git Hooks](git_hooks/README.md)

For contributors, make sure you also consult the following:
- [Code of conduct](docs/code-of-conduct.md)
- [Contributing](docs/contributing.md)
