#include <glog/logging.h>

#include <cstdio>

#include "benchmark/benchmark.h"

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
