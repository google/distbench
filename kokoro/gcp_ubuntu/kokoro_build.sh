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

# This overrides -repo_env=CC=gcc-11 --repo_env=CXX=g++-11 from .bazelrc:
function bazel_basic {
  echo bazel "${@}"
  bazel "${@}"
}

function test_targets() {
  bazel_basic test --disk_cache=/tmpfs/bazel_disk_cache --test_output=errors "${@}" "${CONFIG[@]}" ||
  bazel_basic test --disk_cache=/tmpfs/bazel_disk_cache --test_output=errors "${@}" "${CONFIG[@]}" ||
  bazel_basic test --disk_cache=/tmpfs/bazel_disk_cache --test_output=errors "${@}" "${CONFIG[@]}"
}

function test_main_targets() {
  local main_targets=(:all)
  echo "Testing targets: ${main_targets[@]}"
  test_targets ${main_targets[@]}
}

cd "${KOKORO_ARTIFACTS_DIR}/github/distbench"

print_header_and_run "Logging host lsb version" \
  lsb_release -a

print_header_and_run "Logging host kernel version" \
  uname -a

print_header_and_run "Logging host ip addresses" \
  ip address

function tool_versions() {
  gcc --version | head -n 1
  g++ --version | head -n 1
  bazel --version
}

print_header_and_run "Logging tool versions" \
  tool_versions

function install_libfabric_mercury() {
  if [ 1 -eq $(ls -d /opt/mercury-* 2>/dev/null | wc -w) ]; then
    rm -rf external_repos
    mkdir --parents external_repos/opt
    ln -s -v /opt/libfabric-* external_repos/opt/libfabric
    ln -s -v /opt/mercury-* external_repos/opt/mercury
  else
    echo Installing libfabric and mercury
    ./setup_mercury.sh
  fi
}

print_header_and_run "Installing libfabric and mercury" \
  install_libfabric_mercury

function unpack_bazel_cache() {
  if ! [ -f "/bazel_disk_cache.tar.bz2" ]; then
    return
  fi

  echo Unpacking Bazel cache
  (
    cd /tmpfs
    time tar xf /bazel_disk_cache.tar.bz2 --use-compress-prog=pbzip2
  )
}
print_header_and_run "Unpack Bazel cache" \
  unpack_bazel_cache

CONFIG=(--//:with-mercury)
print_header_and_run "Bazel test (:all, analysis:all, test_builder:all)" \
  test_targets test_builder:all analysis:all :all

CONFIG=(--//:with-mercury --config=asan)
print_header_and_run "Bazel test - ASAN" \
  test_main_targets

CONFIG=(--//:with-mercury --config=tsan)
print_header_and_run "Bazel test - TSAN" \
  test_main_targets

