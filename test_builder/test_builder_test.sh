#!/bin/bash
set -eu pipefail

function compare_golden_file() {
  # strip out the first part of the invocation line, which looks like either:
  # produced by ./test_builder -o test_builder_golden_configs ...
  # or
  # produced by ./test_builder -o - ...
  local invocation="$(head -n 1 "$1" | cut -f 7- "-d ")"
  read invocation <<< $invocation  # un-escape the string
  # Invoke the current code with the same arguments and compare its output to
  # the reference output:
  diff -u -I '^# produced by ' "$1" <(../test_builder -o - ${invocation})
}

PATH=$PATH:${PWD}/external/homa_module
which dist_to_proto
test_builder/test_builder -h

cd "./test_builder/test_builder_golden_configs"
for config in *.config; do
  echo checking ${config}
  ../../distbench check_test --infile $PWD/${config}
done

for f in *.config; do
  echo Comparing against "$f":
  if compare_golden_file "$f"; then
    echo "Output matches! :-)"
  else
    echo "Output does not match! :-("
    exit 1
  fi
done
