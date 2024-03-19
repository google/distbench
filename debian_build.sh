#!/bin/bash
set -eu
DISTBENCH_SRC=$(readlink -f $(dirname $(which $0)))
cd "${DISTBENCH_SRC}"
source common_status.sh

BAZELISK_URL="https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-linux-amd64"
echo_magenta "Installing dependencies..."
bazel version || (
  curl -L -o bazel "${BAZELISK_URL}"
  chmod +x bazel
  sudo mv bazel /usr/local/bin
)

packages=(g++-11 python3-numpy python3-matplotlib python3-seaborn)
echo_magenta "Checking for dependencies"
sleep 1
dpkg --status "${packages[@]}" || (
  echo_cyan "\n\nInstalling missing dependencies"
  sleep 3;
  sudo apt-get update -y
  sudo apt-get install -y "${packages[@]}"
)

./setup_mercury.sh

echo_magenta "Building Distbench..."
bazel build //:all //analysis:all "@homa_module//:dist_to_proto" --//:with-mercury=true --//:with-homa=true -c opt

rm -rf bin
mkdir -p bin
cp test_builder/test_builder bin
cp bazel-bin/distbench bin
cp bazel-bin/analysis/results_conversion bin
cp bazel-bin/external/homa_module/dist_to_proto bin

chmod u+w bin/*
chmod a+x bin/*
set +x
echo_green "Build was successful, binaries are in $(readlink -sf bin)"
echo_green "You can add this directory to your path, or copy the files elsewhere."
echo_green "E.g. cp bin/* /usr/local/bin"
