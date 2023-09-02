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

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"
#include "distbench_test_sequencer_tester.h"
#include "distbench_thread_support.h"
#include "distbench_utils.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "protocol_driver_allocator.h"

namespace distbench {

TestSequence IntenseTrafficTestSequence(const char* protocol) {
  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();
  test->set_default_protocol(protocol);
  auto* s1 = test->add_services();
  s1->set_name("s1");
  s1->set_count(1);
  auto* s2 = test->add_services();
  s2->set_name("s2");
  s2->set_count(5);

  auto* l1 = test->add_action_lists();
  l1->set_name("s1");
  l1->add_action_names("s1/ping");

  auto a1 = test->add_actions();
  a1->set_name("s1/ping");
  a1->set_rpc_name("echo");

  auto* iterations = a1->mutable_iterations();
  iterations->set_max_duration_us(200'000);
  iterations->set_max_iteration_count(2000);
  iterations->set_max_parallel_iterations(100);

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("echo");
  r1->add_client("s1");
  r1->set_server("s2");

  auto* l2 = test->add_action_lists();
  l2->set_name("echo");
  return test_sequence;
}

void RunIntenseTrafficMaxDurationMaxIteration(const char* protocol) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());
  TestSequence test_sequence = IntenseTrafficTestSequence(protocol);
  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/200);
  LOG(INFO) << results.status().message();
  ASSERT_OK(results.status());
}

void RunIntenseTrafficMaxDuration(const char* protocol) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());
  TestSequence test_sequence = IntenseTrafficTestSequence(protocol);
  auto* iterations =
      test_sequence.mutable_tests(0)->mutable_actions(0)->mutable_iterations();
  iterations->clear_max_iteration_count();
  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/200);
  LOG(INFO) << results.status().message();
  ASSERT_OK(results.status());
}

void RunIntenseTrafficMaxIteration(const char* protocol) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());
  TestSequence test_sequence = IntenseTrafficTestSequence(protocol);
  auto* iterations =
      test_sequence.mutable_tests(0)->mutable_actions(0)->mutable_iterations();
  iterations->clear_max_iteration_count();
  iterations->clear_max_duration_us();
  iterations->set_max_iteration_count(2000);
  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/200);
  LOG(INFO) << results.status().message();
  ASSERT_OK(results.status());
}

TEST(IntenseTrafficTest, MaxDurationGrpc) {
  RunIntenseTrafficMaxDuration("grpc");
}

TEST(IntenseTrafficTest, MaxDurationGrpcAsyncCallback) {
  RunIntenseTrafficMaxDuration("grpc_async_callback");
}

TEST(IntenseTrafficTest, MaxIterationGrpc) {
  RunIntenseTrafficMaxIteration("grpc");
}

TEST(IntenseTrafficTest, MaxIterationGrpcAsyncCallback) {
  RunIntenseTrafficMaxIteration("grpc_async_callback");
}

TEST(IntenseTrafficTest, MaxDurationMaxIterationGrpc) {
  RunIntenseTrafficMaxDurationMaxIteration("grpc");
}

TEST(IntenseTrafficTest, MaxDurationMaxIterationGrpcAsyncCallback) {
  RunIntenseTrafficMaxDurationMaxIteration("grpc_async_callback");
}

#ifdef WITH_MERCURY
TEST(IntenseTrafficTest, MaxDurationMercury) {
  RunIntenseTrafficMaxDuration("mercury");
}

TEST(IntenseTrafficTest, MaxIterationMercury) {
  RunIntenseTrafficMaxIteration("mercury");
}

TEST(IntenseTrafficTest, MaxDurationMaxIterationMercury) {
  RunIntenseTrafficMaxDurationMaxIteration("mercury");
}
#endif
}  // namespace distbench
