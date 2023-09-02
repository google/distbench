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

TEST(OpenLoopTest, CliqueTest) {
  int nb_cliques = 3;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();

  auto* pd_opts = test->add_protocol_driver_options();
  pd_opts->set_name("default_protocol_driver_options");
  pd_opts->set_netdev_name("lo");

  auto* s1 = test->add_services();
  s1->set_name("clique");
  s1->set_count(nb_cliques);

  auto* l1 = test->add_action_lists();
  l1->set_name("clique");
  l1->add_action_names("clique_queries");

  auto a1 = test->add_actions();
  a1->set_name("clique_queries");
  a1->mutable_iterations()->set_max_duration_us(2'000'000);
  a1->mutable_iterations()->set_open_loop_interval_ns(3'200'000);
  a1->mutable_iterations()->set_open_loop_interval_distribution("sync_burst");
  a1->set_rpc_name("clique_query");

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("clique_query");
  r1->add_client("clique");
  r1->set_server("clique");
  r1->set_fanout_filter("all");

  auto* l2 = test->add_action_lists();
  l2->set_name("clique_query");

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  ASSERT_EQ(results.value().test_results().size(), 1);
  auto& test_results = results.value().test_results(0);

  const auto& log_summary = test_results.log_summary();
  const auto& latency_summary = log_summary[1];
  size_t pos = latency_summary.find("N: ") + 3;
  ASSERT_NE(pos, std::string::npos);
  const std::string N_value = latency_summary.substr(pos);

  std::string N_value2 = N_value.substr(0, N_value.find(' '));
  int N;
  ASSERT_EQ(absl::SimpleAtoi(N_value2, &N), true);
  int min = 624 * (nb_cliques * (nb_cliques - 1));
  int max = 626 * (nb_cliques * (nb_cliques - 1));
  ASSERT_LE(N, max);
  ASSERT_GE(N, min);

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 3);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("clique/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

TEST(OpenLoopTest, ExponenentialDistributionTest) {
  const int nominal_interval = 16'000'000;
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();
  test->set_default_protocol("grpc");

  auto* s1 = test->add_services();
  s1->set_name("exponential_client");
  s1->set_count(1);

  auto* s2 = test->add_services();
  s2->set_name("exponential_server");
  s2->set_count(1);

  auto* l1 = test->add_action_lists();
  l1->set_name("exponential_client");
  l1->add_action_names("exp_queries");

  auto a1 = test->add_actions();
  a1->set_name("exp_queries");
  a1->mutable_iterations()->set_max_duration_us(2'000'000);
  a1->mutable_iterations()->set_open_loop_interval_ns(nominal_interval);
  a1->mutable_iterations()->set_open_loop_interval_distribution("exponential");
  a1->set_rpc_name("exp_query");

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("exp_query");
  r1->add_client("exponential_client");
  r1->set_server("exponential_server");

  auto* l2 = test->add_action_lists();
  l2->set_name("exp_query");

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  ASSERT_EQ(results.value().test_results().size(), 1);
  auto& test_results = results.value().test_results(0);

  const auto& log_summary = test_results.log_summary();
  const auto& latency_summary = log_summary[1];
  size_t pos = latency_summary.find("N: ") + 3;
  ASSERT_NE(pos, std::string::npos);
  const std::string N_value = latency_summary.substr(pos);

  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("exponential_client/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
  auto exp_server =
      instance_results_it->second.peer_logs().find("exponential_server/0");
  ASSERT_NE(exp_server, instance_results_it->second.peer_logs().end());
  auto exp_server_echo = exp_server->second.rpc_logs().find(0);
  ASSERT_NE(exp_server_echo, exp_server->second.rpc_logs().end());
  ASSERT_TRUE(exp_server_echo->second.failed_rpc_samples().empty());

  // Store the timestamps of the rpcs in the timestamps vector
  int N = exp_server_echo->second.successful_rpc_samples_size();
  std::vector<int64_t> timestamps;
  timestamps.reserve(N);
  for (auto& rpc_sample : exp_server_echo->second.successful_rpc_samples()) {
    timestamps.push_back(rpc_sample.start_timestamp_ns());
  }

  // Find the time intervals between the timestamps, and the maximum, minimum
  // and avg
  std::sort(timestamps.begin(), timestamps.end());
  int64_t max_ts = 0;
  int64_t min_ts = std::numeric_limits<int64_t>::max();
  int64_t avg = 0;
  for (size_t i = 0; i < timestamps.size() - 1; i++) {
    int64_t interval = timestamps[i + 1] - timestamps[i];
    avg += interval;
    if (interval > max_ts) {
      max_ts = interval;
    }
    if (interval < min_ts) {
      min_ts = interval;
    }
  }
  avg /= (N - 1);

  LOG(INFO) << "MIN: " << min_ts;
  LOG(INFO) << "MAX: " << max_ts;
  LOG(INFO) << "AVG: " << avg;
  // Assert that the range of the intervals is wide enough
  ASSERT_LE(min_ts, 0.25 * nominal_interval);
  ASSERT_GE(max_ts, 3 * nominal_interval);

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

TEST(OpenLoopTest, ConstantDistributionTest) {
  const int nominal_interval = 25'000'000;
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();
  test->set_default_protocol("grpc");

  auto* s1 = test->add_services();
  s1->set_name("constant_client");
  s1->set_count(1);

  auto* s2 = test->add_services();
  s2->set_name("constant_server");
  s2->set_count(1);

  auto* l1 = test->add_action_lists();
  l1->set_name("constant_client");
  l1->add_action_names("constant_queries");

  auto a1 = test->add_actions();
  a1->set_name("constant_queries");
  a1->mutable_iterations()->set_max_duration_us(2'000'000);
  a1->mutable_iterations()->set_open_loop_interval_ns(nominal_interval);
  a1->mutable_iterations()->set_open_loop_interval_distribution("constant");
  a1->mutable_iterations()->set_warmup_iterations(50);
  a1->set_rpc_name("constant_query");

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("constant_query");
  r1->add_client("constant_client");
  r1->set_server("constant_server");

  auto* l2 = test->add_action_lists();
  l2->set_name("constant_query");

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  ASSERT_EQ(results.value().test_results().size(), 1);
  auto& test_results = results.value().test_results(0);

  const auto& log_summary = test_results.log_summary();
  const auto& latency_summary = log_summary[1];
  size_t pos = latency_summary.find("N: ") + 3;
  ASSERT_NE(pos, std::string::npos);
  const std::string N_value = latency_summary.substr(pos);

  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("constant_client/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
  auto constant_server =
      instance_results_it->second.peer_logs().find("constant_server/0");
  ASSERT_NE(constant_server, instance_results_it->second.peer_logs().end());
  auto constant_server_echo = constant_server->second.rpc_logs().find(0);
  ASSERT_NE(constant_server_echo, constant_server->second.rpc_logs().end());
  ASSERT_TRUE(constant_server_echo->second.failed_rpc_samples().empty());

  // Store the timestamps of the rpcs in the timestamps vector
  int N = constant_server_echo->second.successful_rpc_samples_size();
  std::vector<int64_t> timestamps;
  timestamps.reserve(N);
  for (auto& rpc_sample :
       constant_server_echo->second.successful_rpc_samples()) {
    if (!rpc_sample.warmup()) {
      timestamps.push_back(rpc_sample.start_timestamp_ns());
    }
  }

  // Find the time intervals between the timestamps, and the maximum, minimum
  // and avg
  std::sort(timestamps.begin(), timestamps.end());

  double variance = 0;
  for (size_t i = 0; i < timestamps.size() - 1; i++) {
    int64_t interval = timestamps[i + 1] - timestamps[i];
    variance += pow(interval - nominal_interval, 2);
    LOG(INFO) << "deviation = " << interval - nominal_interval;
  }
  variance /= timestamps.size() - 1;
  LOG(INFO) << "VARIANCE: " << variance;

  // Assert that the standard deviation is low
  ASSERT_LE(sqrt(variance), 0.25 * nominal_interval);

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

}  // namespace distbench
