#!/bin/bash
set -x
if ! which bazel || ! which grpc_cli
then
  echo DistBench demo requires bazel and grpc_cli be installed.
  exit 1
fi
echo Attempting to build DistBench...
if ! bazel build :distbench -c opt
then
  echo DistBench did not build successfully.
  exit 2
else
  echo DistBench built, starting up an instance on localhost...
  bazel run :distbench -c opt -- test_sequencer --port=10000&
  bazel run :distbench -c opt -- node_manager --test_sequencer=localhost:10000 --port=9999 &
  sleep 5
  if grpc_cli --channel_creds_type=insecure ls localhost:10000
  then
    echo DistBench is running.
    echo Try running simple_test.sh to give it a test to execute.
    wait
  else
    echo dunno what went wrong
  fi
fi
