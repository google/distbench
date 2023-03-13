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

#include "absl/base/internal/sysinfo.h"
#include "absl/synchronization/notification.h"
#include "distbench_thread_support.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"

namespace {

using distbench::CreateThreadpool;

class OverloadTest : public testing::TestWithParam<std::string> {};

TEST_P(OverloadTest, Overload) {
  absl::Notification overload_waiter;
  {
    auto atp = CreateThreadpool(GetParam(), absl::base_internal::NumCPUs());
    ASSERT_TRUE(atp.ok());
    int overload_threshhold = absl::base_internal::NumCPUs();
    distbench::SetOverloadAbortThreshhold(overload_threshhold);
    distbench::SetOverloadAbortCallback([&]() {
      LOG(INFO) << "detected overload";
      overload_waiter.Notify();
      LOG(INFO) << "and notified " << &overload_waiter;
    });
    for (int i = 0; i < 2 * overload_threshhold; ++i) {
      atp.value()->AddTask([&]() {
        overload_waiter.WaitForNotificationWithTimeout(absl::Seconds(1));
      });
    }
  }
  ASSERT_TRUE(overload_waiter.HasBeenNotified()) << &overload_waiter;
};

INSTANTIATE_TEST_SUITE_P(OverloadTests, OverloadTest,
                         testing::Values("null", "elastic"));

class ThreadpoolTest : public testing::TestWithParam<std::string> {};

TEST(ThreadpoolTest, BadType) {
  auto atp = CreateThreadpool("bad_type", 4);
  ASSERT_FALSE(atp.ok());
};

TEST_P(ThreadpoolTest, Constructor) {
  auto atp = CreateThreadpool(GetParam(), 4);
  ASSERT_TRUE(atp.ok());
};

TEST_P(ThreadpoolTest, PerformSimpleWork) {
  const int iterations = 1000;
  std::atomic<int> work_counter = 0;
  {
    auto atp = CreateThreadpool(GetParam(), 4);
    ASSERT_TRUE(atp.ok());
    for (int i = 0; i < iterations; i++) {
      atp.value()->AddTask([&]() { ++work_counter; });
    }
  }  // Complete the work of atp.
  ASSERT_EQ(work_counter, iterations);
};

TEST_P(ThreadpoolTest, ParallelAddTest) {
  const int iterations = 1000;
  std::atomic<int> work_counter = 0;
  {
    auto atp_work_performer = CreateThreadpool(GetParam(), 4);
    ASSERT_TRUE(atp_work_performer.ok());
    {
      auto atp_work_generator = CreateThreadpool(GetParam(), 4);
      ASSERT_TRUE(atp_work_generator.ok());
      for (int i = 0; i < iterations; i++) {
        atp_work_generator.value()->AddTask([&]() {
          atp_work_performer.value()->AddTask([&]() { ++work_counter; });
        });
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
