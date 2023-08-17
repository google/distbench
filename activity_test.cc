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
#include "absl/strings/str_replace.h"
#include "distbench_node_manager.h"
#include "distbench_test_sequencer_tester.h"
#include "distbench_utils.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "protocol_driver_allocator.h"

namespace distbench {

struct TestSequenceParams {
  int nb_cliques = 2;
  bool open_loop = true;
  std::string activity_name = "";
  std::string activity_name_2 = "";
  std::string activity_func = "";
  std::string activity_func_2 = "";
  bool duplicate_activity_config = false;
  bool activity_with_same_config = false;
  bool invalid_config = false;
  int array_size = 0;
  int max_iteration_count = -1;
};

void AddActivity(DistributedSystemDescription* test, ActionList* action_list,
                 std::string activity_name, std::string activity_func,
                 TestSequenceParams params) {
  auto activity_config = absl::StrCat(activity_name, "_Config");

  // Add activity-action to action list.
  action_list->add_action_names(activity_name);
  if (params.activity_with_same_config)
    action_list->add_action_names(activity_name + "_duplicate");

  if (activity_name == "unknown_activity") return;

  // Add the description of activity.
  auto a = test->add_actions();
  a->set_name(activity_name);
  a->set_activity_config_name(activity_config);
  if (params.max_iteration_count != -1) {
    a->mutable_iterations()->set_max_iteration_count(
        params.max_iteration_count);
  } else {
    a->mutable_iterations()->set_max_duration_us(600'000'000);
  }
  if (params.activity_with_same_config) {
    auto a = test->add_actions();
    a->set_name(activity_name + "_duplicate");
    a->set_activity_config_name(activity_config);
    a->mutable_iterations()->set_max_duration_us(600'000'000);
  }

  if (activity_name == "known_activity_unknown_config") return;

  // Add the settings of activity.
  auto* ac = test->add_activity_configs();
  ac->set_name(activity_config);
  AddActivitySettingStringTo(ac, "activity_func", activity_func);
  if (params.invalid_config)
    AddActivitySettingIntTo(ac, "array_size", -1);
  else
    AddActivitySettingIntTo(ac, "array_size", params.array_size);
}

TestSequence GetCliqueTestSequence(const TestSequenceParams& params) {
  int nb_cliques = params.nb_cliques;
  bool open_loop = params.open_loop;
  std::string activity_name = params.activity_name;
  std::string activity_name_2 = params.activity_name_2;
  std::string activity_func = params.activity_func;
  std::string activity_func_2 = params.activity_func_2;
  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();

  auto* lo_opts = test->add_protocol_driver_options();
  lo_opts->set_name("lo_opts");
  lo_opts->set_netdev_name("lo");

  auto* s1 = test->add_services();
  s1->set_name("clique");
  s1->set_count(nb_cliques);
  s1->set_protocol_driver_options_name("lo_opts");

  auto* l1 = test->add_action_lists();
  l1->set_name("clique");
  l1->add_action_names("clique_queries");

  auto a1 = test->add_actions();
  a1->set_name("clique_queries");
  a1->mutable_iterations()->set_max_duration_us(2'000'000);
  if (open_loop) {
    a1->mutable_iterations()->set_open_loop_interval_ns(3'200'000);
    a1->mutable_iterations()->set_open_loop_interval_distribution("sync_burst");
  }
  a1->set_rpc_name("clique_query");
  a1->set_cancel_traffic_when_done(true);

  if (!activity_name.empty()) {
    AddActivity(test, l1, activity_name, activity_func, params);
    if (params.duplicate_activity_config) {
      auto* ac = test->add_activity_configs();
      ac->set_name(absl::StrCat(activity_name, "_Config"));
      AddActivitySettingStringTo(ac, "activity_func", activity_func);
      AddActivitySettingIntTo(ac, "array_size", params.array_size);
    }
  }

  if (!activity_name_2.empty()) {
    AddActivity(test, l1, activity_name_2, activity_func_2, params);
  }

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("clique_query");
  r1->set_client("clique");
  r1->set_server("clique");
  r1->set_fanout_filter("all");

  auto* l2 = test->add_action_lists();
  l2->set_name("clique_query");

  return test_sequence;
}

void CheckCpuConsumeIterationCnt(const TestSequenceResults& results,
                                 int expected_iteration_cnt_lower_bound = 0,
                                 int expected_activity_num = 0,
                                 bool exact_match = false) {
  for (const auto& res : results.test_results()) {
    for (const auto& [instance_name, instance_log] :
         res.service_logs().instance_logs()) {
      if (expected_iteration_cnt_lower_bound == 0) {
        EXPECT_EQ(instance_log.activity_logs().size(), 0);
      } else {
        EXPECT_GT(instance_log.activity_logs().size(), 0);
      }

      EXPECT_EQ(instance_log.activity_logs_size(), expected_activity_num);

      for (const auto& [activity_name, activity_log] :
           instance_log.activity_logs()) {
        LOG(INFO) << activity_name;
        LOG(INFO) << activity_log.DebugString();
        for (const auto& metric : activity_log.activity_metrics()) {
          EXPECT_EQ(metric.name(), "iteration_count");
          if (metric.name() == "iteration_count") {
            if (exact_match) {
              EXPECT_EQ(metric.value_int(), expected_iteration_cnt_lower_bound);
            } else {
              EXPECT_GT(metric.value_int(), expected_iteration_cnt_lower_bound);
            }
          }
        }
      }
    }
  }
}

TEST(DistBenchTestSequencer, ServerActivityTest) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();

  auto* client = test->add_services();
  client->set_name("client");
  client->set_count(1);
  client->set_protocol_driver_options_name("lo_opts");

  auto* server = test->add_services();
  server->set_name("server");
  server->set_count(1);
  server->set_protocol_driver_options_name("lo_opts");

  auto* rpc_desc = test->add_rpc_descriptions();
  rpc_desc->set_name("client_server_rpc");
  rpc_desc->set_client("client");
  rpc_desc->set_server("server");
  rpc_desc->set_request_payload_name("request_payload");
  rpc_desc->set_response_payload_name("response_payload");

  auto* client_al = test->add_action_lists();
  client_al->set_name("client");
  client_al->add_action_names("run_queries");

  auto* lo_opts = test->add_protocol_driver_options();
  lo_opts->set_name("lo_opts");
  lo_opts->set_netdev_name("lo");

  auto action = test->add_actions();
  action->set_name("run_queries");
  action->set_rpc_name("client_server_rpc");
  action->mutable_iterations()->set_max_iteration_count(5);

  auto* server_al = test->add_action_lists();
  server_al->set_name("client_server_rpc");
  server_al->add_action_names("MyConsumeCpu");

  auto server_action = test->add_actions();
  server_action->set_name("MyConsumeCpu");
  server_action->set_activity_config_name("ConsumeCpuConfig");
  server_action->mutable_iterations()->set_max_iteration_count(1);

  auto* server_ac = test->add_activity_configs();
  server_ac->set_name("ConsumeCpuConfig");
  AddActivitySettingStringTo(server_ac, "activity_func", "ConsumeCpu");
  AddActivitySettingIntTo(server_ac, "array_size", 10);

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
  ASSERT_EQ(N, 5);

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 2);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("server/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
  const auto& activity_log_it =
      instance_results_it->second.activity_logs().find("ConsumeCpuConfig");
  ASSERT_EQ(activity_log_it->second.activity_metrics(0).name(),
            "iteration_count");
  ASSERT_EQ(activity_log_it->second.activity_metrics(0).value_int(), 5);
}

#if 0
// The tests in this section are flaky.
TEST(DistBenchTestSequencer, CliqueOpenLoopRpcAntagonistTest) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.activity_name = "ConsumeCpu2500";
  params.activity_name_2 = "ConsumeCpu2500_2";
  params.activity_func = "ConsumeCpu";
  params.activity_func_2 = "ConsumeCpu";
  params.array_size = 2500;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  CheckCpuConsumeIterationCnt(results.value(), 100, 2);

  // The remainder of this test checks the same
  // things as CliqueTest.
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
  LOG(INFO) << "Total N is: " << N;

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), nb_cliques);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("clique/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

TEST(DistBenchTestSequencer, CliqueClosedLoopRpcAntagonistTest) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.open_loop = false;
  params.activity_name = "ConsumeCpu250";
  params.activity_func = "ConsumeCpu";
  params.array_size = 250;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  CheckCpuConsumeIterationCnt(results, 100, 1);

  // The remainder of this test checks the same
  // things as CliqueTest.
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
  int min = 100 * (nb_cliques * (nb_cliques - 1));
  ASSERT_GE(N, min);
  LOG(INFO) << "Total N is: " << N;

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), nb_cliques);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("clique/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

TEST(DistBenchTestSequencer, PolluteDataCache) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.open_loop = false;
  params.activity_name = "PolluteDataCache2M";
  params.activity_func = "PolluteDataCache";
  params.array_size = 2'000'000;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  CheckCpuConsumeIterationCnt(results, 100, 1);

  // The remainder of this test checks the same
  // things as CliqueTest.
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
  int min = 100 * (nb_cliques * (nb_cliques - 1));
  ASSERT_GE(N, min);
  LOG(INFO) << "Total N is: " << N;

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), nb_cliques);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("clique/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

TEST(DistBenchTestSequencer, PolluteInstructionCache) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.open_loop = false;
  params.activity_name = "myPolluteInstructionCache";
  params.activity_func = "PolluteInstructionCache";
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  CheckCpuConsumeIterationCnt(results.value(), 100, 1);

  // The remainder of this test checks the same
  // things as CliqueTest.
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
  int min = 100 * (nb_cliques * (nb_cliques - 1));
  ASSERT_GE(N, min);
  LOG(INFO) << "Total N is: " << N;

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), nb_cliques);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("clique/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

TEST(DistBenchTestSequencer, ConsumeCpuWithMaxIterationCount) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.open_loop = false;
  params.activity_name = "ConsumeCpu2500";
  params.activity_func = "ConsumeCpu";
  params.array_size = 2500;
  params.max_iteration_count = 10;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  CheckCpuConsumeIterationCnt(results.value(), 10, 1, true);

  // The remainder of this test checks the same
  // things as CliqueTest.
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
  int min = 100 * (nb_cliques * (nb_cliques - 1));
  ASSERT_GE(N, min);
  LOG(INFO) << "Total N is: " << N;

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), nb_cliques);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("clique/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}

TEST(DistBenchTestSequencer, TwoActivitiesWithSameActivityConfig) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.activity_name = "ConsumeCpu2500";
  params.activity_func = "ConsumeCpu";
  params.array_size = 2500;
  params.activity_with_same_config = true;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_OK(results.status());

  CheckCpuConsumeIterationCnt(results.value(), 100, 1);

  // The remainder of this test checks the same
  // things as CliqueTest.
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
  LOG(INFO) << "Total N is: " << N;

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), nb_cliques);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("clique/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
}
#endif

TEST(DistBenchTestSequencer, UnknownActivity) {
  int nb_cliques = 3;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.activity_name = "unknown_activity";
  params.activity_func = "ConsumeCpu";
  params.array_size = 2500;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_EQ(results.status().code(), absl::StatusCode::kAborted);
}

TEST(DistBenchTestSequencer, KnownActivityUnknownConfig) {
  int nb_cliques = 3;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.open_loop = true;
  params.activity_name = "known_activity_unknown_config";
  params.activity_func = "ConsumeCpu";
  params.array_size = 2500;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_EQ(results.status().code(), absl::StatusCode::kAborted);
}

TEST(DistBenchTestSequencer, RedefineActivityConfig) {
  int nb_cliques = 3;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.open_loop = false;
  params.activity_name = "ConsumeCpu2500";
  params.activity_func = "ConsumeCpu";
  params.array_size = 2500;
  params.duplicate_activity_config = true;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_EQ(results.status().code(), absl::StatusCode::kAborted);
}

TEST(DistBenchTestSequencer, PreCheckInvalidActivityConfig) {
  int nb_cliques = 3;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  TestSequenceParams params;
  params.nb_cliques = nb_cliques;
  params.activity_name = "ConsumeCpu2500";
  params.activity_func = "ConsumeCpu";
  params.array_size = 2500;
  params.invalid_config = true;
  auto test_sequence = GetCliqueTestSequence(params);

  auto results = tester.RunTestSequence(test_sequence, /*timeout_s=*/75);
  ASSERT_EQ(results.status().code(), absl::StatusCode::kAborted);
}

}  // namespace distbench
