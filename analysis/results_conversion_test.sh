#!/bin/bash
set -eu pipefail

BINDIR=$(dirname $0)
RESULT_DIR="$PWD/analysis"
TEST_FILE="$RESULT_DIR/golden_results/client_server_test/client_server_1x1x1-grpc_polling_inline.pb"

$BINDIR/results_conversion --output_directory=$TEST_TMPDIR --input_file=$TEST_FILE

if diff -x '*.pb' -x '*.config' -r "$TEST_TMPDIR" "$RESULT_DIR/golden_results/client_server_test"; then
    echo "Output matches! :-)"
  else
    echo "Output does not match! :-("
    exit 1
fi