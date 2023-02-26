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

export CXX=g++
export CC=gcc
BAZEL_VERSION=5.4.0
PATH="$HOME/bin:$PATH"

cd "${KOKORO_ARTIFACTS_DIR}/github/distbench"

print_header_and_run "Downloading and installing Bazel version $BAZEL_VERSION" \
  bazel_install

print_header_and_run "Installing libnum-dev" \
  apt-get install libnuma-dev -y

print_header_and_run "Logging host lsb version" \
  lsb_release -a

print_header_and_run "Logging host kernel version" \
  uname -a

print_header_and_run "Logging host ip addresses" \
  ip address

print_header_and_run "Logging $CC version" \
  $CC --version

print_header_and_run "Logging $CXX version" \
  $CXX --version

print_header_and_run "Logging Bazel version" \
  bazel --version

print_header_and_run "Installing libfabric and mercury" \
  ./setup_mercury.sh

print_header_and_run "Bazel fetch" \
  run_with_retries bazel fetch :all

print_header_and_run "Bazel test test_builder" \
  bazel test test_builder:all

print_header_and_run "Bazel build" \
  bazel build :all --//:with-mercury

print_header_and_run "Bazel test" \
  bazel test --test_output=errors :all --//:with-mercury

print_header_and_run "Bazel test - ASAN" \
  bazel test --test_output=errors :all --//:with-mercury --config=asan

print_header_and_run "Bazel test - TSAN" \
  bazel test --test_output=errors :all --//:with-mercury --config=tsan

print_header_and_run "Bazel shutdown" \
  bazel shutdown
