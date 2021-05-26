# Distbench

## Introduction

This benchmark is made to evaluate Remote Procedure Calls (RPCs) stacks, as
RPCs represent an important amount of data communication in the data center.

Using realistic and widely used traffic pattern, the whole RPCs stack can
be evaluated: data serialization/de-serialization, remote calls, thread
wake-ups, eventually compression/decompression, encryption/decryption...

## Getting started

To build Distbench, you need to have Bazel installed; follow the instructions
for your distribution at https://docs.bazel.build/versions/master/install.html
to install Bazel.

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

## Source Code Headers

Every file containing source code must include copyright and license
information. This includes any JS/CSS files that you might be serving out to
browsers. (This is to help well-intentioned people avoid accidental copying that
doesn't comply with the license.)

Apache header:

    Copyright 2021 Google LLC

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        https://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
