#!/bin/bash
set -ex
TOPDIR="$PWD"
rm -f homa.ko
cd "$(bazel info output_base)/external/homa_module"
make -C "/lib/modules/$(uname -r)/build/" M=$(pwd) modules -k -j
cp homa.ko "$TOPDIR"
