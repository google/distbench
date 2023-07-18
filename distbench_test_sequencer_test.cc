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

#include "distbench_test_sequencer.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "distbench_node_manager.h"
#include "distbench_test_sequencer_tester.h"
#include "distbench_thread_support.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "protocol_driver_allocator.h"

namespace distbench {

TEST(DistBenchTestSequencer, Constructor) { TestSequencer test_sequencer; }

TEST(DistBenchTestSequencer, Initialization) {
  distbench::TestSequencerOpts ts_opts = {};
  int port = 0;
  ts_opts.port = &port;
  TestSequencer test_sequencer;
  test_sequencer.Initialize(ts_opts);
  test_sequencer.Shutdown();
}

TEST(DistBenchTestSequencer, EmptyGroup) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(0));
}

TEST(DistBenchTestSequencer, NonEmptyGroup) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(3));

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();
  auto* s1 = test->add_services();
  s1->set_name("s1");
  s1->set_count(1);
  auto* s2 = test->add_services();
  s2->set_name("s2");
  s2->set_count(2);

  auto* l1 = test->add_action_lists();
  l1->set_name("s1");
  l1->add_action_names("s1/ping");

  auto a1 = test->add_actions();
  a1->set_name("s1/ping");
  a1->set_rpc_name("echo");
  a1->mutable_iterations()->set_max_iteration_count(10);

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("echo");
  r1->set_client("s1");
  r1->set_server("s2");

  auto* l2 = test->add_action_lists();
  l2->set_name("echo");

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/70);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  ASSERT_OK(status);

  ASSERT_EQ(results.test_results().size(), 1);
  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("s1/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
  auto s2_0 = instance_results_it->second.peer_logs().find("s2/0");
  ASSERT_NE(s2_0, instance_results_it->second.peer_logs().end());
  auto s2_0_echo = s2_0->second.rpc_logs().find(0);
  ASSERT_NE(s2_0_echo, s2_0->second.rpc_logs().end());
  ASSERT_TRUE(s2_0_echo->second.failed_rpc_samples().empty());
  ASSERT_EQ(s2_0_echo->second.successful_rpc_samples_size(), 10);

  auto s2_1 = instance_results_it->second.peer_logs().find("s2/1");
  ASSERT_NE(s2_1, instance_results_it->second.peer_logs().end());
  auto s2_1_echo = s2_1->second.rpc_logs().find(0);
  ASSERT_NE(s2_1_echo, s2_1->second.rpc_logs().end());
  ASSERT_TRUE(s2_1_echo->second.failed_rpc_samples().empty());
  ASSERT_EQ(s2_1_echo->second.successful_rpc_samples_size(), 10);
}

TEST(DistBenchTestSequencer, Overload) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));
  const std::string proto = R"(
tests {
  overload_limits {
    max_threads: 4
  }
  protocol_driver_options {
    name: 'default_protocol_driver_options'
    protocol_name: 'grpc'
    server_settings {
      name: 'server_type'
      string_value: 'handoff'
    }
  }
  services {
    name: "client"
    count: 1
  }
  services {
    name: "server"
    count: 1
  }
  action_lists {
    name: "client"
    action_names: "run_overloading_queries"
  }
  actions {
    name: "run_overloading_queries"
    rpc_name: "overload_query"
    iterations {
      max_iteration_count: 100
      max_parallel_iterations: 100
    }
  }
  rpc_descriptions {
    name: "overload_query"
    client: "client"
    server: "server"
  }
  action_lists {
    name: "overload_query"
    action_names: "simulate_overload"
  }
  actions {
    name: "simulate_overload"
    activity_config_name: "simulate_overload_activity"
  }
  activity_configs {
    name: "simulate_overload_activity"
    activity_settings {
      name: "activity_func"
      string_value: "SleepFor"
    }
    activity_settings {
      name: "duration_us"
      int64_value: 10000000
    }
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/75);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);

  auto& test_results = results.test_results(0);
  auto service_logs = test_results.service_logs();
  ASSERT_EQ(service_logs.instance_logs().size(), 1);
  EXPECT_EQ(service_logs.instance_logs().begin()->first, "client/0");
  auto& log = service_logs.instance_logs().begin()->second;
  ASSERT_EQ(log.engine_error_message(),
            "RESOURCE_EXHAUSTED: Too many threads running");
  ASSERT_EQ(log.peer_logs_size(), 1);
  auto peer_log = log.peer_logs().begin()->second.rpc_logs().at(0);
  EXPECT_GE(peer_log.successful_rpc_samples_size(), 4);
  EXPECT_LE(peer_log.successful_rpc_samples_size(), 30);
  EXPECT_LE(peer_log.failed_rpc_samples_size(), 96);
  ASSERT_GE(peer_log.failed_rpc_samples_size(), 70);
  EXPECT_EQ(peer_log.failed_rpc_samples().at(0).error_index(), 1);
  EXPECT_EQ(log.error_dictionary().error_message_size(), 2);
  EXPECT_TRUE(log.error_dictionary().error_message().at(0).empty());
  EXPECT_EQ(log.error_dictionary().error_message().at(1),
            "Traffic cancelled: RESOURCE_EXHAUSTED: Too many threads running");
}

TEST(DistBenchTestSequencer, TestReservoirSampling) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();
  test->set_default_protocol("grpc");
  auto* s1 = test->add_services();
  s1->set_name("s1");
  s1->set_count(1);
  auto* s2 = test->add_services();
  s2->set_name("s2");
  s2->set_count(1);

  auto* l1 = test->add_action_lists();
  l1->set_name("s1");
  l1->add_action_names("s1/ping");
  l1->set_max_rpc_samples(1000);

  auto a1 = test->add_actions();
  a1->set_name("s1/ping");
  a1->set_rpc_name("echo");

  auto* iterations = a1->mutable_iterations();
  iterations->set_max_parallel_iterations(100);
  iterations->set_max_iteration_count(3000);
  iterations->set_warmup_iterations(1000);

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("echo");
  r1->set_client("s1");
  r1->set_server("s2");

  auto* l2 = test->add_action_lists();
  l2->set_name("echo");

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/200);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  LOG(INFO) << status.error_message();
  ASSERT_OK(status);
  ASSERT_EQ(results.test_results_size(), 1);
  ASSERT_EQ(results.test_results(0).service_logs().instance_logs_size(), 1);
  auto it = results.test_results(0).service_logs().instance_logs().begin();
  EXPECT_EQ(it->first, "s1/0");
  ASSERT_EQ(it->second.peer_logs_size(), 1);
  auto it2 = it->second.peer_logs().begin();
  EXPECT_EQ(it2->first, "s2/0");
  ASSERT_EQ(it2->second.rpc_logs_size(), 1);
  auto it3 = it2->second.rpc_logs().begin();
  EXPECT_EQ(it3->first, 0);
  EXPECT_EQ(it3->second.successful_rpc_samples_size(), 1000);
  EXPECT_EQ(it3->second.failed_rpc_samples_size(), 0);
  int warmup_samples = 0;
  for (const auto& sample : it3->second.successful_rpc_samples()) {
    if (sample.warmup()) {
      warmup_samples++;
    }
  }
  // Hypergeometric distribution total population=3000
  //                             warmup population=1000
  //                             samples=1000
  // => Expected value is 333
  // => Probability less than 1 in a million this fails:
  EXPECT_GT(warmup_samples, 275);
  EXPECT_LT(warmup_samples, 392);
}

TEST(DistBenchTestSequencer, TestWarmupSampling) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(3));

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();
  test->set_default_protocol("grpc");
  auto* s1 = test->add_services();
  s1->set_name("s1");
  s1->set_count(1);
  auto* s2 = test->add_services();
  s2->set_name("s2");
  s2->set_count(1);
  auto* s3 = test->add_services();
  s3->set_name("s3");
  s3->set_count(1);

  auto* l1 = test->add_action_lists();
  l1->set_name("s1");
  l1->add_action_names("s1/ping");
  l1->set_max_rpc_samples(1000);

  auto a1 = test->add_actions();
  a1->set_name("s1/ping");
  a1->set_rpc_name("echo_and_forward");
  a1->mutable_iterations()->set_max_parallel_iterations(100);
  a1->mutable_iterations()->set_max_iteration_count(3000);
  a1->mutable_iterations()->set_warmup_iterations(1000);

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("echo_and_forward");
  r1->set_client("s1");
  r1->set_server("s2");

  auto* l2 = test->add_action_lists();
  l2->set_name("echo_and_forward");
  l2->add_action_names("s2/async_ping");

  auto a2 = test->add_actions();
  a2->set_name("s2/async_ping");
  a2->set_action_list_name("async_echo_action_list");

  auto* l3 = test->add_action_lists();
  l3->set_name("async_echo_action_list");
  l3->add_action_names("s2/ping");

  auto a3 = test->add_actions();
  a3->set_name("s2/ping");
  a3->set_rpc_name("async_echo");

  auto* r2 = test->add_rpc_descriptions();
  r2->set_name("async_echo");
  r2->set_client("s2");
  r2->set_server("s3");

  auto* l4 = test->add_action_lists();
  l4->set_name("async_echo");

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/200);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  LOG(INFO) << status.error_message();
  ASSERT_OK(status);
  ASSERT_EQ(results.test_results_size(), 1);
  ASSERT_EQ(results.test_results(0).service_logs().instance_logs_size(), 2);
  auto it = results.test_results(0).service_logs().instance_logs().find("s1/0");
  ASSERT_NE(it, results.test_results(0).service_logs().instance_logs().end());
  ASSERT_EQ(it->second.peer_logs_size(), 1);
  auto it2 = it->second.peer_logs().begin();
  EXPECT_EQ(it2->first, "s2/0");
  ASSERT_EQ(it2->second.rpc_logs_size(), 1);
  auto it3 = it2->second.rpc_logs().begin();
  EXPECT_EQ(it3->first, 0);
  EXPECT_EQ(it3->second.successful_rpc_samples_size(), 1000);
  EXPECT_EQ(it3->second.failed_rpc_samples_size(), 0);
  int warmup_samples = 0;
  for (const auto& sample : it3->second.successful_rpc_samples()) {
    if (sample.warmup()) {
      warmup_samples++;
    }
  }
  // Hypergeometric distribution total population=3000
  //                             warmup population=1000
  //                             samples=1000
  // => Expected value is 333
  // => Probability less than 1 in a million this fails:
  EXPECT_GT(warmup_samples, 275);
  EXPECT_LT(warmup_samples, 392);

  it = results.test_results(0).service_logs().instance_logs().find("s2/0");
  ASSERT_NE(it, results.test_results(0).service_logs().instance_logs().end());
  ASSERT_EQ(it->second.peer_logs_size(), 1);
  it2 = it->second.peer_logs().begin();
  EXPECT_EQ(it2->first, "s3/0");
  ASSERT_EQ(it2->second.rpc_logs_size(), 1);
  it3 = it2->second.rpc_logs().begin();
  EXPECT_EQ(it3->first, 1);
  EXPECT_EQ(it3->second.successful_rpc_samples_size(), 3000);
  EXPECT_EQ(it3->second.failed_rpc_samples_size(), 0);
  warmup_samples = 0;
  for (const auto& sample : it3->second.successful_rpc_samples()) {
    if (sample.warmup()) {
      warmup_samples++;
    }
  }
  EXPECT_EQ(warmup_samples, 1000);
}

TEST(DistBenchTestSequencer, VariablePayloadSizeTest2dPmf) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(nb_cliques));

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
  rpc_desc->set_distribution_config_name("MyPayloadDistribution");

  auto* client_al = test->add_action_lists();
  client_al->set_name("client");
  client_al->add_action_names("run_queries");

  auto* server_al = test->add_action_lists();
  server_al->set_name("client_server_rpc");

  auto* lo_opts = test->add_protocol_driver_options();
  lo_opts->set_name("lo_opts");
  lo_opts->set_netdev_name("lo");

  auto* req_dist = test->add_distribution_config();
  req_dist->set_name("MyPayloadDistribution");
  for (float i = 1; i < 5; i++) {
    auto* pmf_point = req_dist->add_pmf_points();
    pmf_point->set_pmf(i / 10);

    // Request Payload Size
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i * 11);

    // Response Payload Size
    data_point = pmf_point->add_data_points();
    data_point->set_exact(i * 9);
  }
  req_dist->add_field_names("request_payload_size");
  req_dist->add_field_names("response_payload_size");

  auto action = test->add_actions();
  action->set_name("run_queries");
  action->set_rpc_name("client_server_rpc");
  action->mutable_iterations()->set_max_iteration_count(20);

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/75);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  ASSERT_OK(status);

  ASSERT_EQ(results.test_results().size(), 1);
  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("client/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());

  auto& samples = instance_results_it->second.peer_logs()
                      .find("server/0")
                      ->second.rpc_logs()
                      .find(0)
                      ->second.successful_rpc_samples();

  for (const auto& rpc_sample : samples) {
    ASSERT_EQ(rpc_sample.request_size() % 11, 0);
    ASSERT_NE(rpc_sample.request_size(), 0);
    ASSERT_EQ(rpc_sample.response_size() % 9, 0);
    ASSERT_NE(rpc_sample.response_size(), 0);
  }
  ASSERT_EQ(samples.size(), 20);
}

TEST(DistBenchTestSequencer, VariablePayloadSizeTest1dCdf) {
  int nb_cliques = 2;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(nb_cliques));

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
  rpc_desc->set_distribution_config_name("MyPayloadDistribution");

  auto* client_al = test->add_action_lists();
  client_al->set_name("client");
  client_al->add_action_names("run_queries");

  auto* server_al = test->add_action_lists();
  server_al->set_name("client_server_rpc");

  auto* lo_opts = test->add_protocol_driver_options();
  lo_opts->set_name("lo_opts");
  lo_opts->set_netdev_name("lo");

  auto* req_dist = test->add_distribution_config();
  req_dist->set_name("MyPayloadDistribution");
  for (float i = 0; i < 5; i++) {
    auto* cdf_point = req_dist->add_cdf_points();
    cdf_point->set_cdf(i / 4);
    cdf_point->set_value(i * 11);
  }
  req_dist->add_field_names("payload_size");

  auto action = test->add_actions();
  action->set_name("run_queries");
  action->set_rpc_name("client_server_rpc");
  action->mutable_iterations()->set_max_iteration_count(20);

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/75);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  ASSERT_OK(status);

  ASSERT_EQ(results.test_results().size(), 1);
  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("client/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());

  auto& samples = instance_results_it->second.peer_logs()
                      .find("server/0")
                      ->second.rpc_logs()
                      .find(0)
                      ->second.successful_rpc_samples();

  for (const auto& rpc_sample : samples) {
    ASSERT_EQ(rpc_sample.request_size(), rpc_sample.response_size());
  }
  ASSERT_EQ(samples.size(), 20);
}

TEST(DistBenchTestSequencer, ProtocolDriverOptionsTest) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
  }
  services {
    name: "server"
    count: 1
    protocol_driver_options_name: "loopback_pd"
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_iteration_count: 1000
    }
  }
  action_lists {
    name: "client_server_rpc"
  }
  protocol_driver_options {
    name: "loopback_pd"
    netdev_name: "lo"
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/15);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);

  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
}

TEST(DistBenchTestSequencer, ProtocolDriverOptionsGrpcInlineCallbackTest) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
    protocol_driver_options_name: "grpc_client_cb_server_normal"
  }
  services {
    name: "server"
    count: 1
    protocol_driver_options_name: "grpc_client_cb_server_normal"
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_iteration_count: 1000
    }
  }
  action_lists {
    name: "client_server_rpc"
  }
  protocol_driver_options {
    name: "grpc_client_cb_server_normal"
    protocol_name: "grpc"
    netdev_name: "lo"
    server_settings {
      name: "server_type"
      string_value: "inline"
    }
    client_settings {
      name: "client_type"
      string_value: "callback"
    }
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/15);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);

  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
}

TEST(DistBenchTestSequencer, ProtocolDriverOptionsGrpcHandoffPollingTest) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
    protocol_driver_options_name: "grpc_client_cq_server_cb"
  }
  services {
    name: "server"
    count: 1
    protocol_driver_options_name: "grpc_client_cq_server_cb"
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_iteration_count: 1000
    }
  }
  action_lists {
    name: "client_server_rpc"
  }
  protocol_driver_options {
    name: "grpc_client_cq_server_cb"
    protocol_name: "grpc"
    netdev_name: "lo"
    server_settings {
      name: "server_type"
      string_value: "handoff"
    }
    client_settings {
      name: "client_type"
      string_value: "polling"
    }
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/30);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);

  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
}

TEST(DistBenchTestSequencer, RPCTraceSimple) {
  const std::string proto = R"(
tests {
  name: "rpc_trace_simple"
  services {
    name: "load_balancer"
    count: 1
  }
  services {
    name: "root"
    count: 1
  }
  action_lists {
    name: "load_balancer"
    action_names: "load_balancer/do_queries"
  }
  actions {
    name: "load_balancer/do_queries"
    iterations {
      max_parallel_iterations: 5
      max_iteration_count: 25
    }
    rpc_name: "root_query"
  }
  rpc_descriptions {
    name: "root_query"
    client: "load_balancer"
    server: "root"
    fanout_filter: "round_robin"
    request_payload_name: "root_request_payload"
    response_payload_name: "root_response_payload"
    tracing_interval: 1
  }
  action_lists {
    name: "root_query"
    # NOP
  }
  payload_descriptions {
    name: "root_request_payload"
    size: 128
  }
  payload_descriptions {
    name: "root_response_payload"
    size: 128
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  auto context = CreateContextWithDeadline(/*max_time_s=*/30);
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));
  TestSequenceResults results;
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);
  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);

  // Verify that the samples have the correct trace_context.
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("load_balancer/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
  auto dest = instance_results_it->second.peer_logs().find("root/0");
  ASSERT_NE(dest, instance_results_it->second.peer_logs().end());
  auto dest_rpc = dest->second.rpc_logs().find(0);
  ASSERT_NE(dest_rpc, dest->second.rpc_logs().end());
  ASSERT_TRUE(dest_rpc->second.failed_rpc_samples().empty());
  ASSERT_EQ(dest_rpc->second.successful_rpc_samples_size(), 25);

  int64_t iterations = 0;

  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    ASSERT_EQ(rpc.trace_context().engine_ids_size(), 1);
    ASSERT_EQ(rpc.trace_context().action_iterations_size(), 1);
    ASSERT_EQ(rpc.trace_context().actionlist_invocations_size(), 1);
    ASSERT_EQ(rpc.trace_context().actionlist_indices_size(), 1);
    ASSERT_EQ(rpc.trace_context().action_indices_size(), 1);
    ASSERT_EQ(rpc.trace_context().fanout_index_size(), 1);
    iterations |= 1 << rpc.trace_context().action_iterations(0);
    EXPECT_EQ(rpc.trace_context().engine_ids(0), 0);
    EXPECT_EQ(rpc.trace_context().actionlist_invocations(0), 0);
    EXPECT_EQ(rpc.trace_context().actionlist_indices(0), 0);
    EXPECT_EQ(rpc.trace_context().action_indices(0), 0);
    EXPECT_EQ(rpc.trace_context().fanout_index(0), 0);
  }
  EXPECT_EQ(iterations, (1 << 25) - 1);
}

TEST(DistBenchTestSequencer, RPCTraceTwoLevels) {
  const std::string proto = R"(
tests {
  name: "rpc_trace_two_levels"
  services {
    name: "load_balancer"
    count: 1
  }
  services {
    name: "root"
    count: 2
  }
  services {
    name: "leaf"
    count: 3
  }
  action_lists {
    name: "load_balancer"
    action_names: "load_balancer/do_queries"
  }
  actions {
    name: "load_balancer/do_queries"
    iterations {
      max_parallel_iterations: 5
      max_iteration_count: 50
      warmup_iterations: 10
    }
    rpc_name: "root_query"
  }
  rpc_descriptions {
    name: "root_query"
    client: "load_balancer"
    server: "root"
    fanout_filter: "round_robin"
    request_payload_name: "root_request_payload"
    response_payload_name: "root_response_payload"
    tracing_interval: 2
  }
  action_lists {
    name: "root_query"
    action_names: "root/root_query_fanout"
  }
  actions {
    name: "root/root_query_fanout"
    rpc_name: "leaf_query"
  }
  rpc_descriptions {
    name: "leaf_query"
    client: "root"
    server: "leaf"
    fanout_filter: "all"
    request_payload_name: "leaf_request_payload"
    response_payload_name: "leaf_response_payload"
  }
  action_lists {
    name: "leaf_query"
    # no actions, NOP
  }
  payload_descriptions {
    name: "root_request_payload"
    size: 512
  }
  payload_descriptions {
    name: "root_response_payload"
    size: 1024
  }
  payload_descriptions {
    name: "leaf_request_payload"
    size: 16384
  }
  payload_descriptions {
    name: "leaf_response_payload"
    size: 6907
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  auto context = CreateContextWithDeadline(/*max_time_s=*/30);
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(6));
  TestSequenceResults results;
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);
  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 3);

  // Verify that the samples have the correct trace_context.
  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("load_balancer/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());

  // Due to the round-robin and tracing_interval, root/0 gets all the tracing.
  auto dest = instance_results_it->second.peer_logs().find("root/0");
  ASSERT_NE(dest, instance_results_it->second.peer_logs().end());
  auto dest_rpc = dest->second.rpc_logs().find(0);
  ASSERT_NE(dest_rpc, dest->second.rpc_logs().end());
  ASSERT_TRUE(dest_rpc->second.failed_rpc_samples().empty());
  ASSERT_EQ(dest_rpc->second.successful_rpc_samples_size(), 25);
  int64_t iterations_bitmask = 0;

  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    ASSERT_EQ(rpc.trace_context().engine_ids_size(), 1);
    ASSERT_EQ(rpc.trace_context().action_iterations_size(), 1);
    ASSERT_EQ(rpc.trace_context().actionlist_invocations_size(), 1);
    ASSERT_EQ(rpc.trace_context().actionlist_indices_size(), 1);
    ASSERT_EQ(rpc.trace_context().action_indices_size(), 1);
    ASSERT_EQ(rpc.trace_context().fanout_index_size(), 1);
    iterations_bitmask |= 1 << (rpc.trace_context().action_iterations(0) / 2);
    EXPECT_EQ(rpc.trace_context().engine_ids(0), 0);
    EXPECT_EQ(rpc.trace_context().actionlist_invocations(0), 0);
    EXPECT_EQ(rpc.trace_context().actionlist_indices(0), 0);
    EXPECT_EQ(rpc.trace_context().action_indices(0), 0);
    EXPECT_EQ(rpc.trace_context().fanout_index(0), 0);
  }
  EXPECT_EQ(iterations_bitmask, (1 << 25) - 1);
  // root/1 gets no tracing.
  dest = instance_results_it->second.peer_logs().find("root/1");
  ASSERT_NE(dest, instance_results_it->second.peer_logs().end());
  dest_rpc = dest->second.rpc_logs().find(0);
  ASSERT_NE(dest_rpc, dest->second.rpc_logs().end());
  ASSERT_TRUE(dest_rpc->second.failed_rpc_samples().empty());
  ASSERT_EQ(dest_rpc->second.successful_rpc_samples_size(), 25);
  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    ASSERT_FALSE(rpc.has_trace_context());
  }

  // Verify that the tracing cascades down to the leaf (root/0->leaf/0).
  const auto& root0_instance_results_it =
      test_results.service_logs().instance_logs().find("root/0");
  ASSERT_NE(root0_instance_results_it,
            test_results.service_logs().instance_logs().end());
  dest = root0_instance_results_it->second.peer_logs().find("leaf/0");
  ASSERT_NE(dest, instance_results_it->second.peer_logs().end());
  dest_rpc = dest->second.rpc_logs().find(1);
  ASSERT_NE(dest_rpc, dest->second.rpc_logs().end());
  ASSERT_TRUE(dest_rpc->second.failed_rpc_samples().empty());
  ASSERT_EQ(dest_rpc->second.successful_rpc_samples_size(), 25);

  iterations_bitmask = 0;
  int64_t invocations_bitmask = 0;
  int64_t fanout_bitmask = 0;

  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    ASSERT_EQ(rpc.trace_context().engine_ids_size(), 2);
    ASSERT_EQ(rpc.trace_context().action_iterations_size(), 2);
    ASSERT_EQ(rpc.trace_context().actionlist_invocations_size(), 2);
    ASSERT_EQ(rpc.trace_context().actionlist_indices_size(), 2);
    ASSERT_EQ(rpc.trace_context().action_indices_size(), 2);
    ASSERT_EQ(rpc.trace_context().fanout_index_size(), 2);
    iterations_bitmask |= 1 << (rpc.trace_context().action_iterations(0) / 2);
    EXPECT_EQ(rpc.trace_context().engine_ids(0), 0);
    EXPECT_EQ(rpc.trace_context().actionlist_invocations(0), 0);
    EXPECT_EQ(rpc.trace_context().actionlist_indices(0), 0);
    EXPECT_EQ(rpc.trace_context().action_indices(0), 0);
    EXPECT_EQ(rpc.trace_context().action_iterations(1), 0);
    EXPECT_EQ(rpc.trace_context().engine_ids(1), 1);
    invocations_bitmask |= 1 << rpc.trace_context().actionlist_invocations(1);
    EXPECT_EQ(rpc.trace_context().actionlist_indices(1), 1);
    EXPECT_EQ(rpc.trace_context().action_indices(1), 0);
    EXPECT_EQ(rpc.trace_context().fanout_index(0), 0);
    fanout_bitmask |= 1ull << rpc.trace_context().fanout_index(1);
  }

  EXPECT_EQ(fanout_bitmask, (1 << 3) - 1);
  EXPECT_EQ(iterations_bitmask, (1 << 25) - 1);
  EXPECT_EQ(invocations_bitmask, (1 << 25) - 1);
}

TEST(DistBenchTestSequencer, AttributeBasedPlacement) {
  std::vector<Attribute> attributes;
  Attribute attribute;

  attribute.set_name("rack");
  attribute.set_value("A");
  attributes.push_back(attribute);
  attributes.push_back(attribute);
  attribute.set_value("B");
  attributes.push_back(attribute);
  attributes.push_back(attribute);

  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();

  auto* client = test->add_services();
  client->set_name("client");
  client->set_count(2);
  client->set_protocol_driver_options_name("lo_opts");

  auto* server = test->add_services();
  server->set_name("server");
  server->set_count(2);
  server->set_protocol_driver_options_name("lo_opts");

  ConstraintList constraint_list;
  auto* constraint_set = constraint_list.add_constraint_sets();
  auto* constraint_a = constraint_set->add_constraints();
  constraint_a->set_attribute_name("rack");
  constraint_a->set_relation(Constraint_Relation_equal);
  constraint_a->add_string_values("A");

  (*test->mutable_service_constraints())["server/0"] = constraint_list;
  (*test->mutable_service_constraints())["client/0"] = constraint_list;

  std::map<std::string, std::vector<Attribute>> node_attributes = {
      {"node0", {attributes[0]}},
      {"node1", {attributes[1]}},
      {"node2", {attributes[2]}},
      {"node3", {attributes[3]}},
  };
  auto maybe_map = ConstraintSolver(test_sequence.tests(0), node_attributes);
  ASSERT_OK(maybe_map.status());

  auto node_service_map = maybe_map.value();

  ServiceBundle service_bundle_1, service_bundle_2;
  service_bundle_1.add_services("server/0");
  service_bundle_2.add_services("client/0");
  EXPECT_EQ(node_service_map["node0"].size(), 1);
  EXPECT_EQ(node_service_map["node1"].size(), 1);
  EXPECT_EQ(node_service_map["node2"].size(), 1);
  EXPECT_EQ(node_service_map["node3"].size(), 1);
  EXPECT_TRUE(*node_service_map["node0"].begin() == "server/0" ||
              *node_service_map["node0"].begin() == "client/0");
  EXPECT_TRUE(*node_service_map["node1"].begin() == "server/0" ||
              *node_service_map["node1"].begin() == "client/0");
  EXPECT_TRUE(*node_service_map["node2"].begin() == "server/1" ||
              *node_service_map["node2"].begin() == "client/1");
  EXPECT_TRUE(*node_service_map["node3"].begin() == "server/1" ||
              *node_service_map["node3"].begin() == "client/1");

  // Unit test for failure. In this test there's only one node with 
  // attribute rack equals A
  node_attributes = {
      {"node0", {attributes[0]}},
      {"node1", {attributes[2]}},
      {"node2", {attributes[2]}},
      {"node3", {attributes[3]}},
  };

  maybe_map = ConstraintSolver(test_sequence.tests(0), node_attributes);
  EXPECT_FALSE(maybe_map.ok());
}

TEST(DistBenchTestSequencer, MultiServerChannelsTest) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(4));

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
  }
  services {
    name: "server"
    count: 3
    protocol_driver_options_name: "loopback_pd"
    multi_server_channels {
      name: "MSC"
      channel_settings { name: "policy", string_value: "round_robin" }
      selected_instances: 0
      selected_instances: 1
      selected_instances: 2
    }
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
    multi_server_channel_name: "MSC"
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_iteration_count: 12
      open_loop_interval_ns: 10000000
    }
  }
  action_lists {
    name: "client_server_rpc"
  }
  protocol_driver_options {
    name: "loopback_pd"
    netdev_name: "lo"
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/15);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);

  const auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  const auto& [client_name, client_log] = *test_results.service_logs().instance_logs().begin();
  ASSERT_EQ(client_log.peer_logs().size(), 3);
  for (const auto& [server_name, per_server_log] : client_log.peer_logs()) {
    LOG(INFO) << server_name;
    LOG(INFO) << per_server_log.DebugString();
    EXPECT_EQ((*per_server_log.rpc_logs().begin()).second.successful_rpc_samples_size(), 4);
  }
}
}  // namespace distbench
