#!/bin/bash
set -eu pipefail

function compare_golden_file() {
  # strip out the first part of the invocation line, which looks like either:
  # produced by ./test_builder -o test_builder_golden_configs ...
  # or
  # produced by ./test_builder -o - ...
  local invocation="$(head -n 1 "$1" | cut -f 7- "-d ")"
  # Invoke the current code with the same arguments and compare its output to
  # the reference output:
  diff -u -I '^# produced by ' <(../test_builder -o - ${invocation}) "$1"
}

cd "./test_builder/test_builder_golden_configs"
for f in *; do
  echo Comparing against "$f":
  if compare_golden_file "$f"; then
    echo "Output matches! :-)"
  else
    echo "Output does not match! :-("
    exit 1
  fi
done
