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

function bazel_install {
  echo
  echo Downloading and installing Bazel version $BAZEL_VERSION
  echo

  rm -rf ~/bazel_install
  mkdir ~/bazel_install
  cd ~/bazel_install

  wget https://github.com/bazelbuild/bazel/releases/download/"${BAZEL_VERSION}"/bazel-"${BAZEL_VERSION}"-installer-linux-x86_64.sh

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
  sudo apt-get update
  sudo apt-get install -y gcc-9 g++-9
}

time {
  update_gcc || sleep 10 || update_gcc || sleep 20 || update_gcc
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
echo Running Bazel build and test
echo

cd "${KOKORO_ARTIFACTS_DIR}/github/distbench"
bazel build --cxxopt='-std=c++17' :all
bazel test --cxxopt='-std=c++17' :all
bazel shutdown

