#!/bin/bash

# Verify that the needed tools are presents
#
if ! which bazel
then
  echo DistBench requires Bazel. See README.md.
  exit 1
fi

if ! which grpc_cli
then
  echo DistBench requires grpc_cli be installed.
  exit 1
fi

# Build Distbench
#
echo Attempting to build DistBench...
if ! bazel build :distbench -c opt
then
  echo DistBench did not build successfully.
  exit 2
fi

# Run a test_sequencer and node_manager instance
#
echo DistBench built, starting up an instance on localhost...
set -x
bazel run :distbench -c opt -- test_sequencer --port=10000 &
bazel run :distbench -c opt -- node_manager --test_sequencer=localhost:10000 --port=9999 &
set +x
sleep 5

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
