# Mercury Protocol Driver

Mercury is RPC framework designed for HPC systems.

[Merury website](https://mercury-hpc.github.io/).

## Preliminary implementation

The implementation is preliminary and not necessarly optimal:
- Performs extra copies
- Do not use bulk data transfers

## Compilation/Installation

The current Bazel workspace expects to find libfabric and mercury-2.0.1 in
../opt/libfabric and ../opt/mercury-2.0.1; you can get a test setup with
the following instructions:

```
cd ..

export DST_LIBFABRIC=`pwd`/opt/libfabric
export DST_LIBFABRIC=`pwd`/opt/mercury-2.0.1
git clone git@github.com:ofiwg/libfabric.git
cd libfabric
./autogen.sh
./configure --prefix $DST_LIBFABRIC --enable-verbs=no --enable-efa=no --disable-usnic
make -j 24
make install
export LD_LIBRARY_PATH=$DST/lib:$LD_LIBRARY_PATH
cd ..

wget
https://github.com/mercury-hpc/mercury/releases/download/v2.0.1/mercury-2.0.1.tar.bz2
tar xvfj mercury-2.0.1.tar.bz2
cd mercury-2.0.1
mkdir build
cd build
cmake -DNA_USE_SM=OFF -DNA_USE_OFI=ON -DOFI_INCLUDE_DIR=$DST/include -DOFI_LIBRARY=$DST/lib -DCMAKE_INSTALL_PREFIX=$DST_MERCURY ..
make -j 20
make install
cd ../../distbench

bazel test :all --//:with-mercury

```
