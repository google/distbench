#!/bin/bash
set -x
set -e

cd ..
DST_LIBFABRIC=$(pwd)/opt/libfabric
DST_MERCURY=$(pwd)/opt/mercury-2.0.1

git clone git@github.com:ofiwg/libfabric.git
cd libfabric
./autogen.sh
./configure --prefix $DST_LIBFABRIC --enable-verbs=no --enable-efa=no --disable-usnic --enable-psm3-verbs=no
make -j 6
make install
cd ..

LD_LIBRARY_PATH=$DST_MERCURY/lib:$LD_LIBRARY_PATH

wget https://github.com/mercury-hpc/mercury/releases/download/v2.0.1/mercury-2.0.1.tar.bz2 -O mercury-2.0.1.tar.bz2
tar xvfj mercury-2.0.1.tar.bz2
cd mercury-2.0.1
mkdir -p build
cd build
cmake DCMAKE_-BUILD_TYPE=Debug -DNA_USE_SM=OFF -DNA_USE_OFI=ON -DOFI_INCLUDE_DIR=$DST_LIBFABRIC/include -DOFI_LIBRARY=$DST_LIBFABRIC/lib -DCMAKE_INSTALL_PREFIX=$DST_MERCURY ..
make -j 6
make install
cd ../../distbench

bazel test :all --//:with-mercury -c dbg
