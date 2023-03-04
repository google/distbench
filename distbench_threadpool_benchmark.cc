// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <atomic>

#include "benchmark/benchmark.h"
#include "distbench_threadpool.h"
#include "glog/logging.h"

namespace {

using distbench::CreateThreadpool;

void threadpool_test(std::string_view type, benchmark::State& state) {
  int n = state.range(0);
  std::atomic<int> work_counter = 0;
  for (auto s : state) {
    auto atp = CreateThreadpool(type, 8);
    for (int i = 0; i < n; i++) {
      atp->AddWork([&]() { ++work_counter; });
    }
  }  // Complete the work of atp.
}

void BM_Elastic(benchmark::State& state) { threadpool_test("elastic", state); }
void BM_Simple(benchmark::State& state) { threadpool_test("simple", state); }
void BM_Mercury(benchmark::State& state) { threadpool_test("mercury", state); }
void BM_CThread(benchmark::State& state) { threadpool_test("cthread", state); }
void BM_Null(benchmark::State& state) { threadpool_test("null", state); }

BENCHMARK(BM_Null)->Range(1, 16384);
BENCHMARK(BM_Elastic)->Range(1, 16384);
BENCHMARK(BM_Simple)->Range(1, 16384);
#ifdef WITH_MERCURY
BENCHMARK(BM_Mercury)->Range(1, 16384);
#endif
BENCHMARK(BM_CThread)->Range(1, 16384);

}  // namespace
