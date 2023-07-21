#!/bin/bash
set -eu pipefail

BINDIR=$(dirname $0)
RESULT_DIR="$PWD/analysis/golden_results"

readonly test1=( \
  "client_server_test/client_server_1x1x1-grpc_polling_inline.pb.gz" \
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

readonly test4=(  \
  "trace_context_test/clique_sync_burst_10x16000x1024x1024-grpc_polling_inline.pb"  \
  "--output_format=trace_context"  \
)

readonly test5=( \
  "multidimensional_test/2dclique_ring_x_sync_burst_5x3x100000x1024x1024-grpc_polling_inline.pb" \
  "--output_format=default" \
)

tests=(test1 test2 test3 test4 test5)

function custom_diff()
{
  diff -u -r -x '*.pb' -x '*.config' -x '*.gz' "$1" "$2"
}

set -x
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
