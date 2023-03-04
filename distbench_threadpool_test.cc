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

#include "distbench_threadpool.h"

#include <atomic>

#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"

namespace {

using distbench::CreateThreadpool;

class ThreadpoolTest : public testing::TestWithParam<std::string> {};

TEST(ThreadpoolTest, BadType) {
  auto atp = CreateThreadpool("bad_type", 4);
  ASSERT_EQ(atp, nullptr);
};

TEST_P(ThreadpoolTest, Constructor) {
  auto atp = CreateThreadpool(GetParam(), 4);
  ASSERT_NE(atp, nullptr);
};

TEST_P(ThreadpoolTest, PerformSimpleWork) {
  const int iterations = 1000;
  std::atomic<int> work_counter = 0;
  {
    auto atp = CreateThreadpool(GetParam(), 4);
    ASSERT_NE(atp, nullptr);
    for (int i = 0; i < iterations; i++) {
      atp->AddWork([&]() { ++work_counter; });
    }
  }  // Complete the work of atp.
  ASSERT_EQ(work_counter, iterations);
};

TEST_P(ThreadpoolTest, ParallelAddTest) {
  const int iterations = 1000;
  std::atomic<int> work_counter = 0;
  {
    auto atp_work_performer = CreateThreadpool(GetParam(), 4);
    ASSERT_NE(atp_work_performer, nullptr);
    {
      auto atp_work_generator = CreateThreadpool(GetParam(), 4);
      ASSERT_NE(atp_work_generator, nullptr);
      for (int i = 0; i < iterations; i++) {
        atp_work_generator->AddWork(
            [&]() { atp_work_performer->AddWork([&]() { ++work_counter; }); });
      }
    }  // Complete the work of atp_work_generator.
  }    // Complete the work of atp_work_performer.
  ASSERT_EQ(work_counter, iterations);
}

INSTANTIATE_TEST_SUITE_P(ThreadpoolTests, ThreadpoolTest,
                         testing::Values("", "null", "simple", "elastic"
#ifdef WITH_MERCURY
                                         ,
                                         "mercury"
#endif
                                         ));

}  // namespace
