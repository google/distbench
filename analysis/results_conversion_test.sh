#!/bin/bash
set -eu pipefail

BINDIR=$(dirname $0)
RESULT_DIR="$PWD/analysis"
TEST_FILE_1="$RESULT_DIR/golden_results/client_server_test/client_server_1x1x1-grpc_polling_inline.pb"

$BINDIR/results_conversion --output_directory=$TEST_TMPDIR --input_file=$TEST_FILE_1 --consider_warmups

if diff -x '*.pb' -x '*.config' -r "$TEST_TMPDIR" "$RESULT_DIR/golden_results/client_server_test"; then
    echo "Test 1 output matches! :-)"
  else
    echo "Test 1 output does not match! :-("
    exit 1
fi

rm $TEST_TMPDIR/* -r
TEST_FILE_2="$RESULT_DIR/golden_results/supress_header_test/multi_level_rpc_2x3x1-grpc_polling_inline.pb"

$BINDIR/results_conversion --output_directory=$TEST_TMPDIR --input_file=$TEST_FILE_2 --supress_header

#just comparing the non-empty directories:
NON_EMPTY_DIR=$(find "$RESULT_DIR/golden_results/supress_header_test" -type d ! -empty)

if diff --brief -x '*.pb' -x '*.config' -r "$TEST_TMPDIR" "$RESULT_DIR/golden_results/supress_header_test" | grep -vF "$NON_EMPTY_DIR"; then
    echo "Test 2 output matches! :-)"
  else
    echo "Test 2 output does not match! :-("
    exit 1
fi