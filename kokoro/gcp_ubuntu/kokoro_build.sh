#!/bin/bash
# Display commands being run.
# WARNING: please only enable 'set -x' if necessary for debugging, and be very
#  careful if you handle credentials (e.g. from Keystore) with 'set -x':
#  statements like "export VAR=$(cat /tmp/keystore/credentials)" will result in
#  the credentials being printed in build logs.
#  Additionally, recursive invocation with credentials as command-line
#  parameters, will print the full command, with credentials, in the build logs.
# set -x

# Code under repo is checked out to ${KOKORO_ARTIFACTS_DIR}/github.
# The final directory name in this path is determined by the scm name specified
# in the job configuration.

set -uEeo pipefail
shopt -s inherit_errexit

function unknown_error_shutdown() {
  echo -e "\\nError, unknown_error_shutdown invoked status = $?" 1>&2
  echo -e "\\n$BASH_COMMAND" 1>&2
  exit 1
}

trap unknown_error_shutdown ERR

function print_header_and_run {
  echo
  echo '\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\'
  echo "        $1"...
  echo '///////////////////////////////////////////////////////////////////////'
  shift
  echo "\$ $*"
  "$@"
}

#This is a band-aid, should fix root cause.....
function run_with_retries {
  local MAX_TRIES=3
  local DELAY=5
  for i in $(seq 1 $MAX_TRIES); do
    echo "\$ $*"
    "$@" && break

    echo -n "Command failed with errcode $? -"
    echo "try $i / $MAX_TRIES - sleeping $DELAY before retrying..."
    sleep $DELAY
    DELAY=$(( DELAY * 2 ))
  done
}

function bazel_install {
  # Remove the cache has sometime they conflict between versions
  rm -rf  ~/.cache/bazel

  local BAZEL_FILE="bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh"
  local BAZEL_URL="https://github.com/bazelbuild/bazel/releases/download/"
  BAZEL_URL+="${BAZEL_VERSION}/${BAZEL_FILE}"
  run_with_retries wget --no-verbose "${BAZEL_URL}" -O "/tmp/${BAZEL_FILE}"
  bash "/tmp/${BAZEL_FILE}" --user
}

# This overrides -repo_env=CC=gcc-11 --repo_env=CXX=g++-11 from .bazelrc:
function bazel_basic {
  echo bazel "${@}"
  bazel "${@}"
}

function test_targets() {
  for target in "${@}"; do
    bazel_basic test --test_output=errors "$target" "${CONFIG[@]}" ||
    bazel_basic test --test_output=errors "$target" "${CONFIG[@]}" ||
    bazel_basic test --test_output=errors "$target" "${CONFIG[@]}"
  done
}

function test_main_targets() {
  local main_targets=(:distbench_test_sequencer_test :all)
  echo "Testing targets: ${main_targets[@]}"
  test_targets ${main_targets[@]}
}

BAZEL_VERSION=5.4.0
PATH="$HOME/bin:$PATH"

cd "${KOKORO_ARTIFACTS_DIR}/github/distbench"

print_header_and_run "Downloading and installing Bazel version $BAZEL_VERSION" \
  bazel_install

print_header_and_run "Logging host lsb version" \
  lsb_release -a

print_header_and_run "Logging host kernel version" \
  uname -a

print_header_and_run "Logging host ip addresses" \
  ip address

print_header_and_run "Logging gcc version" \
  gcc --version

print_header_and_run "Adding repo for g++-11" \
  add-apt-repository -y ppa:ubuntu-toolchain-r/test

print_header_and_run "Installing libnuma-dev g++-11 gcc-11" \
  apt-get install libnuma-dev g++-11 gcc-11 -y

print_header_and_run "Logging g++ version" \
  g++-11 --version

print_header_and_run "Logging Bazel version" \
  bazel --version

print_header_and_run "Installing libfabric and mercury" \
  ./setup_mercury.sh

print_header_and_run "Bazel fetch" \
  run_with_retries bazel fetch :all

print_header_and_run "Bazel test test_builder" \
  test_targets test_builder:all

print_header_and_run "Bazel test analysis" \
  test_targets analysis:all

print_header_and_run "Bazel build" \
  bazel_basic build :all --//:with-mercury

CONFIG=(--//:with-mercury)
print_header_and_run "Bazel test" \
  test_main_targets

CONFIG=(--//:with-mercury --config=asan)
print_header_and_run "Bazel test - ASAN" \
  test_main_targets

CONFIG=(--//:with-mercury --config=tsan)
print_header_and_run "Bazel test - TSAN" \
  test_main_targets

print_header_and_run "Bazel shutdown" \
  bazel shutdown
