#!/bin/bash
################################################################################
# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
#
# setup_mercury.sh
# Script for setting up libfabric and themercury RPC framework for use with
# distbench.
#
# Homepages:
#   https://ofiwg.github.io/libfabric/
#   https://mercury-hpc.github.io/
#
# Github source:
#   https://github.com/ofiwg/libfabric
#   https://github.com/mercury-hpc/mercury
set -uEeo pipefail
shopt -s inherit_errexit

source git_clone_or_update.sh
ECHODELAY=0 source common_status.sh

LIBFABRIC_VERSION=${1:-1.17.0}
MERCURY_VERSION=${2:-2.2.0}

if [[ $# -gt 2 ]]
then
  echo_error red "$0 takes at most 2 arguments"
  exit 1
fi

function install_libfabric() {
  VERSION_TAG=v${LIBFABRIC_VERSION}
  LIBFABRIC_REPO_DIR=${PWD}/libfabric_repo
  LIBFABRIC_BUILD_DIR=${LIBFABRIC_REPO_DIR}/${LIBFABRIC_VERSION}
  git_clone_or_update \
    https://github.com/ofiwg/libfabric.git \
    ${VERSION_TAG} \
    ${LIBFABRIC_REPO_DIR} \
    ${LIBFABRIC_BUILD_DIR}
  rm -rf ${LIBFABRIC_INSTALL_DIR} opt/libfabric
  (
    cd ${LIBFABRIC_BUILD_DIR}
    if [[ ! -f ./configure ]]
    then
      ./autogen.sh
    fi
    if [[ ! -f config.status ]]
    then
      ./configure \
      --prefix $LIBFABRIC_INSTALL_DIR \
      --enable-verbs=no \
      --enable-efa=no \
      --disable-usnic \
      --enable-psm3-verbs=no || rm config.status
    fi
    echo_magenta "\\nBuilding libfabric..."
    nice make -j $(nproc)
    echo_magenta "\\nInstalling libfabric..."
    make install
  )
  ln -sf ${LIBFABRIC_INSTALL_DIR} opt/libfabric
}

function install_mercury() {
  if [[ -v LD_LIBRARY_PATH && -n "${LD_LIBRARY_PATH}" ]]
  then
    LD_LIBRARY_PATH=$MERCURY_INSTALL_DIR/lib:$LD_LIBRARY_PATH
  else
    LD_LIBRARY_PATH=$MERCURY_INSTALL_DIR/lib
  fi

  VERSION_TAG=v${MERCURY_VERSION}
  MERCURY_REPO_DIR=${PWD}/mercury_repo
  MERCURY_BUILD_DIR=${MERCURY_REPO_DIR}/${MERCURY_VERSION}
  git_clone_or_update \
    https://github.com/mercury-hpc/mercury.git \
    ${VERSION_TAG} \
    ${MERCURY_REPO_DIR} \
    ${MERCURY_BUILD_DIR}
  rm -rf ${MERCURY_INSTALL_DIR} opt/mercury
  (
    cd ${MERCURY_BUILD_DIR}
    if [[ ! -f build/Makefile ]]
    then
      rm -rf build
    fi
    if ! false
    then
      echo_magenta "\\nConfiguring mercury..."
      mkdir -p build
      cd build
      cmake \
        DCMAKE_-BUILD_TYPE=Debug \
        -DNA_USE_SM=OFF \
        -DNA_USE_OFI=ON \
        -DOFI_INCLUDE_DIR=$LIBFABRIC_INSTALL_DIR/include \
        -DOFI_LIBRARY=$LIBFABRIC_INSTALL_DIR/lib \
        -DCMAKE_INSTALL_PREFIX=$MERCURY_INSTALL_DIR \
        ..
    fi
    echo_magenta "\\nBuilding mercury..."
    nice make -j $(nproc)
    echo_magenta "\\nInstalling mercury..."
    make install
  )
  ln -sf ${MERCURY_INSTALL_DIR} opt/mercury
}

function unknown_error_shutdown() {
  echo -e "\\nError, unknown_error_shutdown invoked status = $?"
  echo -e "\\n$BASH_COMMAND"
  exit 1
}

function main() {
  trap unknown_error_shutdown ERR

  mkdir -p external_repos/opt
  cd external_repos
  LIBFABRIC_INSTALL_DIR=$(pwd)/opt/libfabric-${LIBFABRIC_VERSION}
  MERCURY_INSTALL_DIR=$(pwd)/opt/mercury-${MERCURY_VERSION}

  install_libfabric
  install_mercury
}

main
