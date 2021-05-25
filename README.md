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
bazel build --cxxopt='-std=c++17' :all
```

## Running unit tests

```bash
bazel test --cxxopt='-std=c++17' :distbench_test_sequencer_test
bazel test --cxxopt='-std=c++17' :distbench_engine_test
```

## Testing

Run a simple test as follows:

```bash
bazel run --cxxopt='-std=c++17' :distbench -- test_sequencer
bazel run --cxxopt='-std=c++17' :distbench -- node_manager --test_sequencer=localhost:10000 --port=9999
./simple_test.sh
```

To compile and run with debugging enabled:

```bash
bazel run --cxxopt='-std=c++17' --compilation_mode=dbg :distbench -- test_sequencer
bazel run --cxxopt='-std=c++17' --compilation_mode=dbg :distbench -- node_manager --test_sequencer=localhost:10000 --port=9999
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
