#!/bin/bash
set -eu pipefail

BINDIR=$(dirname $0)
RESULT_DIR="$PWD/analysis/golden_results"

readonly test1=( \
  "client_server_test/client_server_1x1x1-grpc_polling_inline.pb" \
  "--consider_warmups" \
)

readonly test2=( \
  "supress_header_test/multi_level_rpc_2x3x1-grpc_polling_inline.pb" \
  "--supress_header" \
)

readonly test3=( \
  "statistics_format_test/multi_level_rpc_2x3x1-grpc_polling_inline.pb" \
  "--output_format=statistics" \
)

tests=(test1 test2 test3)

function custom_diff()
{
  diff -u -r -x '*.pb' -x '*.config' "$1" "$2"
}

for test in "${tests[@]}"; do
  declare -n testparams="$test"
  rm $TEST_TMPDIR/* -rf
  INPUT="${RESULT_DIR}/${testparams[0]}"
  GOLDEN="$(dirname ${RESULT_DIR}/${testparams[0]})"
  $BINDIR/results_conversion \
    --output_directory=$TEST_TMPDIR \
    --input_file="${INPUT}" "${testparams[1]}"

  if custom_diff "$TEST_TMPDIR" "${GOLDEN}"; then
    echo "$test output matches! :-)"
  else
    echo "$test output does not match! :-("
    exit 1
  fi
done

exit 0
