// Copyright 2022 Google LLC
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

#include "distbench_threadpool.h"

#include "gtest/gtest.h"
#include "gtest_utils.h"

namespace {

TEST(DistBenchThreadPool, Constructor) {
  distbench::DistbenchThreadpool dtp{4};
};

TEST(DistBenchThreadPool, PerformSimpleWork) {
  std::atomic<int> work_counter = 0;
  {
    distbench::DistbenchThreadpool dtp{4};
    for (int i = 0; i < 1000; i++) {
      dtp.AddWork([&]() { ++work_counter; });
    }
  }  // Complete the work of dtp.
  ASSERT_EQ(work_counter, 1000);
};

TEST(DistBenchThreadPool, ParallelAddTest) {
  std::atomic<int> work_counter = 0;
  distbench::DistbenchThreadpool dtp_work_performer{4};
  distbench::DistbenchThreadpool dtp_work_generator{4};
  for (int i = 0; i < 1000; i++) {
    dtp_work_generator.AddWork(
      [&]() { dtp_work_performer.AddWork([&]() { ++work_counter; }); });
  }
  sleep(5);
  ASSERT_EQ(work_counter, 1000);
}

}  // namespace
