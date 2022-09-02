#!/bin/bash

# Fail on any error.
set -e

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

export BAZEL_VERSION=4.2.1

function print_and_run {
  echo "\$ $*"
  "$@"
}

function run_with_retries {
  MAX_TRIES=3
  DELAY=5
  for i in $(seq 1 $MAX_TRIES); do
    echo "\$ $*"
    "$@" && break

    echo Command failed with errcode $? - try $i / $MAX_TRIES - sleeping $DELAY before retrying...
    sleep $DELAY
    DELAY=$(( DELAY * 2 ))
  done
}

function bazel_install {
  echo
  echo Downloading and installing Bazel version $BAZEL_VERSION
  echo

  rm -rf ~/bazel_install
  mkdir ~/bazel_install
  cd ~/bazel_install

  run_with_retries wget --no-verbose https://github.com/bazelbuild/bazel/releases/download/"${BAZEL_VERSION}"/bazel-"${BAZEL_VERSION}"-installer-linux-x86_64.sh

  chmod +x bazel-*.sh
  ./bazel-"${BAZEL_VERSION}"-installer-linux-x86_64.sh --user
  rm bazel-"${BAZEL_VERSION}"-installer-linux-x86_64.sh
  cd ~/

  # Remove the cache has sometime they conflict between versions
  rm -rf  ~/.cache/bazel
}


function update_gcc() {
  echo
  echo Installing gcc version 9
  echo
  sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
  sudo apt-get update -q
  sudo apt-get install -q -o Dpkg::Use-Pty=0 -y gcc-9 g++-9
}

time {
  run_with_retries update_gcc
}

function install_clang_and_check_file_formatting() {
  sudo apt install clang-format
  echo "$(pwd)"
  echo "$(ls)"
  echo "$(cat Makefile)"
  make clang-format
  BADLY_FORMATTED_FILES=$(git status --untracked-files=no --porcelain | sed s/^...//)
  if [[ $BADLY_FORMATTED_FILES ]]; then
      echo Following files must be formatted with clang-format:
      echo "$BADLY_FORMATTED_FILES"
      echo Please run 'make clang-format' before commiting.
      exit 1
  fi
}

export CXX=g++-9
export CC=gcc-9

bazel_install
export PATH="$HOME/bin:$PATH"

echo
echo Tool versions
echo
$CC --version
bazel --version

echo
echo Running Bazel fetch
echo
cd "${KOKORO_ARTIFACTS_DIR}/github/distbench"

echo
echo Checking file formatting
echo
install_clang_and_check_file_formatting

print_and_run run_with_retries bazel fetch :all

echo
echo Running Bazel build
echo
print_and_run bazel build :all

echo
echo Running Bazel test
echo
print_and_run bazel test --test_output=errors :all

echo
echo Running Bazel test - ASAN
echo
print_and_run bazel test --config=asan --test_output=errors :all

echo
echo End of the tests
print_and_run bazel shutdown

