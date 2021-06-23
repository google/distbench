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
  if ! which bazel
  then
    echo DistBench requires Bazel. See README.md.
    exit 1
  fi

  if ! which grpc_cli
  then
    echo DistBench requires grpc_cli. See README.md.
    exit 1
  fi
}

build_distbench() {
  # Build Distbench
  #
  echo Attempting to build DistBench...
  if ! bazel build :distbench -c opt
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

show_help() {
  echo "Usage: $0 [-h] [-v] [-n node_manager_cnt]"
  echo "   Run Distbench locally."
  echo
  echo "   -h               Display the usage help (this)"
  echo "   -v               Enable verbose mode"
  echo "   -d               Enable debug mode"
  echo "   -n node_manager_cnt Specify the number of node manager to start"
  echo "                      default: $DEFAULT_NODE_MANAGER_COUNT"
  echo
}

while getopts "h?vdn:" opt; do
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

check_dependencies

# Run a test_sequencer and node_manager instance
#
echo DistBench built, starting up an instance on localhost...
if [[ "${DEBUG}" = "1" ]]; then
  echo_and_run bazel build --compilation_mode=dbg :all
  run_gdb_backtrace bazel-bin/distbench test_sequencer --port=10000 --ports_to_use 10001-10100 &
  sleep 3
  run_gdb_backtrace
  for i in $(seq 1 1 $NODE_MANAGER_COUNT)
  do
    run_gdb_backtrace bazel-bin/distbench node_manager --test_sequencer=localhost:10000 --port=$((9999-$i)) --ports_to_use $((11100 + $(($i*100)) ))-$((11199 + $(($i*100)) )) &
  done
  sleep 5
else
  build_distbench

  echo Starting the Distbench test sequencer
  echo_and_run bazel run :distbench -c opt -- test_sequencer --port=10000 &
  sleep 3

  echo Starting $NODE_MANAGER_COUNT Distbench node managers
  for i in $(seq 1 1 $NODE_MANAGER_COUNT)
  do
    echo_and_run bazel run :distbench -c opt -- node_manager --test_sequencer=localhost:10000 --port=$((9999-$i)) --ports_to_use $((11100 + $(($i*100)) ))-$((11199 + $(($i*100)) )) &
  done
  sleep 5
fi

# Verify that Distbench is up and running
#
if ! grpc_cli --channel_creds_type=insecure ls localhost:10000
then
  echo Error could not run grpc_cli ls on the the local instance.
  exit 3
fi

echo DistBench is running.
echo Try running simple_test.sh to give it a test to execute.
wait
