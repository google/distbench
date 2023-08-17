#include <cstdio>

#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "benchmark/benchmark.h"

int main(int argc, char** argv) {
  absl::InitializeLog();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
