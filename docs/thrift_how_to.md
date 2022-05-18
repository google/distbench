# Running Distbench with Thrift

## Introduction

[Apache Thrift](https://thrift.apache.org/) can be used as transport for the
RPCs using the thrift protocol driver.

Note: Thrift is only used as a transport for the RPC the
serialization/deserialization is still performed using Protobuf.

## Building the Thrift library

Build the Apache Thrift library in the parent folder `opt/thrift`.

```bash
cd ..
git clone https://github.com/apache/thrift.git thrift_src
cd thrift_src
./bootstrap.sh
./configure --without-java --without-go --without-python --without-py3 --prefix=`pwd`/../opt/thrift
make
make check
make install
```

Run the Distbench tests:
```bash
cd ../distbench
make test_with_thrift
```

## Running with the Thrift protocol driver

Use the `thrift` protocol driver:
```
tests {
  default_protocol: "thrift"
```

Run the test:
```bash
export DISTBENCH_EXTRA_BAZEL_OPTIONS="--//:with-thrift"
./start_distbench_localhost.sh -n 1
./simple_test.sh
```


