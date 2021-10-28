#!/bin/bash

# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

check_dependencies() {
  # Verify that the needed tools are presents
  #
  if ! which bazel > /dev/null
  then
    echo DistBench requires Bazel. See README.md.
    exit 1
  fi
}

build_distbench() {
  # Build Distbench
  #
  echo Attempting to build DistBench...
  if ! echo_and_run bazel build :distbench $BAZEL_COMPILATION_OPTIONS
  then
    echo DistBench did not build successfully.
    exit 2
  fi
}

OPTIND=1

# Script parameters
VERBOSE=0
DEBUG=0
DEFAULT_NODE_MANAGER_COUNT=1
NODE_MANAGER_COUNT=$DEFAULT_NODE_MANAGER_COUNT
BAZEL_COMPILATION_OPTIONS="-c opt" # --config=basicprof

show_help() {
  echo "Usage: $0 [-h] [-v] [-n node_manager_cnt]"
  echo "   Run Distbench locally."
  echo
  echo "   -h               Display the usage help (this)"
  echo "   -v               Enable verbose mode"
  echo "   -d               Enable debug mode"
  echo "   -n node_manager_cnt Specify the number of node manager to start"
  echo "                      default: $DEFAULT_NODE_MANAGER_COUNT"
  echo "   -c compile_opt   Compile option (\"-c opt\", \"--config=asan\")"
  echo
}

while getopts "h?vdn:c:" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    v)  VERBOSE=1
        ;;
    d)  DEBUG=1
        ;;
    n)  NODE_MANAGER_COUNT=$OPTARG
        ;;
    c)  BAZEL_COMPILATION_OPTIONS=$OPTARG
        ;;
    esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift

if [[ "${VERBOSE}" = "1" ]]; then
  echo Run Distbench locally
  echo "  VERBOSE=$VERBOSE"
  echo "  NODE_MANAGER_COUNT=$NODE_MANAGER_COUNT"
  echo
fi

echo_and_run() {
  if [[ "${VERBOSE}" = "1" ]]; then
    echo
    echo "\$ $*"
  fi
  "$@"
}

run_gdb_backtrace() {
  echo Running "$*" under gdb
  gdb -batch -ex "run" -ex "bt" --args "$@" 2>&1 | grep -v ^"No stack."$
}

export GLOG_logtostderr=1
export GLOG_minloglevel=0

check_dependencies
build_distbench

echo Starting Distbench test sequencer and $NODE_MANAGER_COUNT node managers
if [[ "${DEBUG}" = "1" ]]; then
  run_gdb_backtrace bazel-bin/distbench test_sequencer --port=10000 &
  sleep 3
  for i in $(seq 1 1 $NODE_MANAGER_COUNT); do
    run_gdb_backtrace bazel-bin/distbench \
      node_manager --test_sequencer=localhost:10000 \
                   --port=$((9999-$i)) --default_data_plane_device=lo &
  done
else
  echo_and_run bazel run :distbench $BAZEL_COMPILATION_OPTIONS -- \
    test_sequencer --port=10000 --local_nodes=$NODE_MANAGER_COUNT &
fi

sleep 3

# Verify that Distbench is up and running
echo | bazel run :distbench $BAZEL_COMPILATION_OPTIONS -- \
                  run_tests --test_sequencer=localhost:10000

if [ $? -ne 0 ]; then
  echo Error could not connect to the distbench test sequencer.
  echo Something went wrong while starting Distbench...
  exit 3
fi

echo DistBench is running.
echo Try running simple_test.sh to give it a test to execute.
wait
