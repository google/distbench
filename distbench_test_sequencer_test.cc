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

#include "absl/strings/str_replace.h"
#include "distbench_node_manager.h"
#include "distbench_thread_support.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "protocol_driver_allocator.h"

namespace distbench {

std::unique_ptr<grpc::ClientContext> CreateContextWithDeadline(int max_time_s) {
  auto context = std::make_unique<grpc::ClientContext>();
  SetGrpcClientContextDeadline(context.get(), max_time_s);
  return context;
}

struct DistBenchTester {
  ~DistBenchTester();
  absl::Status Initialize(int num_nodes);

  std::unique_ptr<TestSequencer> test_sequencer;
  std::unique_ptr<DistBenchTestSequencer::Stub> test_sequencer_stub;
  std::vector<std::unique_ptr<NodeManager>> nodes;
};

DistBenchTester::~DistBenchTester() {
  TestSequence test_sequence;
  test_sequence.mutable_tests_setting()->set_shutdown_after_tests(true);
  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/10);
  grpc::Status status = test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  if (!status.ok()) {
    ADD_FAILURE() << "RunTestSequence RPC failed " << status;
    test_sequencer->Shutdown();
  }
  test_sequencer->Wait();
  for (size_t i = 0; i < nodes.size(); ++i) {
    nodes[i]->Wait();
  }
  SetOverloadAbortThreshhold(0);
}

absl::Status DistBenchTester::Initialize(int num_nodes) {
  test_sequencer = std::make_unique<TestSequencer>();
  distbench::TestSequencerOpts ts_opts = {};
  int port = 0;
  ts_opts.port = &port;
  test_sequencer->Initialize(ts_opts);
  nodes.resize(num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    distbench::NodeManagerOpts nm_opts = {};
    int port = 0;
    // Flip the order of the last few nodes:
    if (i >= 2) {
      nm_opts.preassigned_node_id = num_nodes - i + 1;
    }
    nm_opts.port = &port;
    nm_opts.test_sequencer_service_address = test_sequencer->service_address();
    nodes[i] = std::make_unique<NodeManager>();
    auto ret = nodes[i]->Initialize(nm_opts);
    if (!ret.ok()) return ret;
  }
  SetOverloadAbortCallback([this]() {
    for (const auto& node : nodes) {
      node->CancelTraffic(
          absl::ResourceExhaustedError("Too many threads running"));
    }
  });
  std::shared_ptr<grpc::ChannelCredentials> client_creds =
      MakeChannelCredentials();
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateCustomChannel(test_sequencer->service_address(), client_creds,
                                DistbenchCustomChannelArguments());
  test_sequencer_stub = DistBenchTestSequencer::NewStub(channel);
  return absl::OkStatus();
}

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
  r1->set_client("s1");
  r1->set_server("s2");

  auto* l2 = test->add_action_lists();
  l2->set_name("echo");
  return test_sequence;
}

void RunIntenseTrafficMaxDurationMaxIteration(const char* protocol) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(6));
  TestSequence test_sequence = IntenseTrafficTestSequence(protocol);
  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/200);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  LOG(INFO) << status.error_message();
  ASSERT_OK(status);
}

void RunIntenseTrafficMaxDuration(const char* protocol) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(6));
  TestSequence test_sequence = IntenseTrafficTestSequence(protocol);
  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/200);
  auto* iterations =
      test_sequence.mutable_tests(0)->mutable_actions(0)->mutable_iterations();
  iterations->clear_max_iteration_count();
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  LOG(INFO) << status.error_message();
  ASSERT_OK(status);
}

void RunIntenseTrafficMaxIteration(const char* protocol) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(6));
  TestSequence test_sequence = IntenseTrafficTestSequence(protocol);
  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/200);
  auto* iterations =
      test_sequence.mutable_tests(0)->mutable_actions(0)->mutable_iterations();
  iterations->clear_max_iteration_count();
  iterations->clear_max_duration_us();
  iterations->set_max_iteration_count(2000);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  LOG(INFO) << status.error_message();
  ASSERT_OK(status);
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

TEST(DistBenchTestSequencer, RunIntenseTrafficMaxDurationGrpc) {
  RunIntenseTrafficMaxDuration("grpc");
}

TEST(DistBenchTestSequencer, RunIntenseTrafficMaxDurationGrpcAsyncCallback) {
  RunIntenseTrafficMaxDuration("grpc_async_callback");
}

TEST(DistBenchTestSequencer, RunIntenseTrafficMaxIterationGrpc) {
  RunIntenseTrafficMaxIteration("grpc");
}

TEST(DistBenchTestSequencer, RunIntenseTrafficMaxIterationGrpcAsyncCallback) {
  RunIntenseTrafficMaxIteration("grpc_async_callback");
}

TEST(DistBenchTestSequencer, RunIntenseTrafficMaxDurationMaxIterationGrpc) {
  RunIntenseTrafficMaxDurationMaxIteration("grpc");
}

TEST(DistBenchTestSequencer,
     RunIntenseTrafficMaxDurationMaxIterationGrpcAsyncCallback) {
  RunIntenseTrafficMaxDurationMaxIteration("grpc_async_callback");
}

#ifdef WITH_MERCURY
TEST(DistBenchTestSequencer, RunIntenseTrafficMaxDurationMercury) {
  RunIntenseTrafficMaxDuration("mercury");
}

TEST(DistBenchTestSequencer, RunIntenseTrafficMaxIterationMercury) {
  RunIntenseTrafficMaxIteration("mercury");
}

TEST(DistBenchTestSequencer, RunIntenseTrafficMaxDurationMaxIterationMercury) {
  RunIntenseTrafficMaxDurationMaxIteration("mercury");
}
#endif

TEST(DistBenchTestSequencer, CliqueTest) {
  int nb_cliques = 3;

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(nb_cliques));

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
  a1->mutable_iterations()->set_open_loop_interval_ns(3'200'000);
  a1->mutable_iterations()->set_open_loop_interval_distribution("sync_burst");
  a1->set_rpc_name("clique_query");

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("clique_query");
  r1->set_client("clique");
  r1->set_server("clique");
  r1->set_fanout_filter("all");

  auto* l2 = test->add_action_lists();
  l2->set_name("clique_query");

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/75);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  ASSERT_OK(status);

  ASSERT_EQ(results.test_results().size(), 1);
  auto& test_results = results.test_results(0);

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

TEST(DistBenchTestSequencer, VariablePayloadSizeTest) {
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

  int num_samples = 0;
  for (const auto& rpc_sample : instance_results_it->second.peer_logs()
                                    .find("server/0")
                                    ->second.rpc_logs()
                                    .find(0)
                                    ->second.successful_rpc_samples()) {
    ASSERT_EQ(rpc_sample.request_size() % 11, 0);
    ASSERT_NE(rpc_sample.request_size(), 0);
    ASSERT_EQ(rpc_sample.response_size() % 9, 0);
    ASSERT_NE(rpc_sample.response_size(), 0);
    num_samples++;
  }
  ASSERT_EQ(num_samples, 20);
}

TEST(DistBenchTestSequencer, StochasticTest) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(6));

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
  }
  services {
    name: "server"
    count: 5
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
    request_payload_name: "request_payload"
    response_payload_name: "response_payload"
    fanout_filter: "stochastic{0.7:1,0.3:4}"
  }
  payload_descriptions {
    name: "request_payload"
    size: 196
  }
  payload_descriptions {
    name: "response_payload"
    size: 262144
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
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/75);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, &results);
  ASSERT_OK(status);

  auto& test_results = results.test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);

  const auto& log_summary = test_results.log_summary();
  const auto& latency_summary = log_summary[1];
  size_t pos = latency_summary.find("N: ") + 3;
  ASSERT_NE(pos, std::string::npos);
  const std::string N_value = latency_summary.substr(pos);

  std::string N_value2 = N_value.substr(0, N_value.find(' '));
  int N;
  ASSERT_EQ(absl::SimpleAtoi(N_value2, &N), true);
  ASSERT_LE(N, 2300);
  ASSERT_GE(N, 1500);
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

TEST(DistBenchTestSequencer, FanoutRoundRobinTest) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(5));

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
  }
  services {
    name: "server"
    count: 4
    protocol_driver_options_name: "loopback_pd"
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
    fanout_filter: "round_robin"
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_iteration_count: 1024
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

  auto serv_log_it =
      test_results.service_logs().instance_logs().find("client/0");
  for (int i = 0; i < 4; i++) {
    auto peer_log_it =
        serv_log_it->second.peer_logs().find(absl::StrCat("server/", i));
    auto rpc_log_it = peer_log_it->second.rpc_logs().find(0);
    ASSERT_EQ(rpc_log_it->second.successful_rpc_samples_size(), 256);
  }
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

  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    // The RPC trace should have two entries (@load_balancer/0, @root/0).
    ASSERT_EQ(rpc.trace_context().engine_ids().size(), 2);
    ASSERT_EQ(rpc.trace_context().iterations().size(), 2);
  }
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
  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    ASSERT_EQ(rpc.trace_context().engine_ids().size(), 2);
    ASSERT_EQ(rpc.trace_context().iterations().size(), 2);
  }
  // root/1 gets no tracing.
  dest = instance_results_it->second.peer_logs().find("root/1");
  ASSERT_NE(dest, instance_results_it->second.peer_logs().end());
  dest_rpc = dest->second.rpc_logs().find(0);
  ASSERT_NE(dest_rpc, dest->second.rpc_logs().end());
  ASSERT_TRUE(dest_rpc->second.failed_rpc_samples().empty());
  ASSERT_EQ(dest_rpc->second.successful_rpc_samples_size(), 25);
  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    ASSERT_EQ(rpc.trace_context().engine_ids().size(), 0);
    ASSERT_EQ(rpc.trace_context().iterations().size(), 0);
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
  for (auto rpc : dest_rpc->second.successful_rpc_samples()) {
    ASSERT_EQ(rpc.trace_context().engine_ids().size(), 3);
    ASSERT_EQ(rpc.trace_context().iterations().size(), 3);
  }
}

//These tests do not work very well with the thread sanitizer.
#ifndef THREAD_SANITIZER
TEST(DistBenchTestSequencer, ExponenentialDistributionTest) {
  const int avg_interval = 3'200'000;
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));

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
  a1->mutable_iterations()->set_open_loop_interval_ns(avg_interval);
  a1->mutable_iterations()->set_open_loop_interval_distribution("exponential");
  a1->set_rpc_name("exp_query");

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("exp_query");
  r1->set_client("exponential_client");
  r1->set_server("exponential_server");

  auto* l2 = test->add_action_lists();
  l2->set_name("exp_query");

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/75);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  ASSERT_OK(status);

  ASSERT_EQ(results.test_results().size(), 1);
  auto& test_results = results.test_results(0);

  const auto& log_summary = test_results.log_summary();
  const auto& latency_summary = log_summary[1];
  size_t pos = latency_summary.find("N: ") + 3;
  ASSERT_NE(pos, std::string::npos);
  const std::string N_value = latency_summary.substr(pos);

  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("exponential_client/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
  auto exp_server = instance_results_it->second.peer_logs().find("exponential_server/0");
  ASSERT_NE(exp_server, instance_results_it->second.peer_logs().end());
  auto exp_server_echo = exp_server->second.rpc_logs().find(0);
  ASSERT_NE(exp_server_echo, exp_server->second.rpc_logs().end());
  ASSERT_TRUE(exp_server_echo->second.failed_rpc_samples().empty());

  //Store the timestamps of the rpcs in the timestamps vector
  int N = exp_server_echo->second.successful_rpc_samples_size();
  std::vector<uint64_t> timestamps;
  timestamps.reserve(N);
  for (auto& rpc_sample : exp_server_echo->second.successful_rpc_samples()) {
    timestamps.push_back(rpc_sample.start_timestamp_ns());
  }

  //Find the time intervals between the timestamps, and the maximum, minimum and avg
  std::vector<uint64_t> time_intervals;
  time_intervals.reserve(N - 1);
  uint64_t max_ts = 0;
  uint64_t min_ts = std::numeric_limits<uint64_t>::max();
  uint64_t avg=0;
  uint64_t next_interval;
  for(size_t i = 0; i < timestamps.size() - 1; i++){
    next_interval = timestamps[i + 1] - timestamps[i];
    time_intervals.push_back(next_interval);
    avg += time_intervals[i];
    if (next_interval > max_ts) {
      max_ts = next_interval;
    }
    if (next_interval < min_ts) {
      min_ts = next_interval;
    }
  }
  avg/=(N-1);

  LOG(INFO) << "MIN: " << min_ts;
  LOG(INFO) << "MAX: " << max_ts;
  LOG(INFO) << "AVG: " << avg;
  //Assert that the range of the intervals is wide enough
  ASSERT_LE(min_ts, 0.1 * avg_interval );
  ASSERT_GE(max_ts, 3 * avg_interval );

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  ASSERT_NE(instance_results_it, test_results.service_logs().instance_logs().end());
}

TEST(DistBenchTestSequencer, ConstantDistributionTest) {
  const int avg_interval = 3'200'000;
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize(2));

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
  a1->mutable_iterations()->set_open_loop_interval_ns(avg_interval);
  a1->mutable_iterations()->set_open_loop_interval_distribution("constant");
  a1->set_rpc_name("constant_query");

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("constant_query");
  r1->set_client("constant_client");
  r1->set_server("constant_server");

  auto* l2 = test->add_action_lists();
  l2->set_name("constant_query");

  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/75);
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  ASSERT_OK(status);

  ASSERT_EQ(results.test_results().size(), 1);
  auto& test_results = results.test_results(0);

  const auto& log_summary = test_results.log_summary();
  const auto& latency_summary = log_summary[1];
  size_t pos = latency_summary.find("N: ") + 3;
  ASSERT_NE(pos, std::string::npos);
  const std::string N_value = latency_summary.substr(pos);

  const auto& instance_results_it =
      test_results.service_logs().instance_logs().find("constant_client/0");
  ASSERT_NE(instance_results_it,
            test_results.service_logs().instance_logs().end());
  auto constant_server = instance_results_it->second.peer_logs().find("constant_server/0");
  ASSERT_NE(constant_server, instance_results_it->second.peer_logs().end());
  auto constant_server_echo = constant_server->second.rpc_logs().find(0);
  ASSERT_NE(constant_server_echo, constant_server->second.rpc_logs().end());
  ASSERT_TRUE(constant_server_echo->second.failed_rpc_samples().empty());

  //Store the timestamps of the rpcs in the timestamps vector
  int N = constant_server_echo->second.successful_rpc_samples_size();
  std::vector<uint64_t> timestamps;
  timestamps.reserve(N);
  for (auto& rpc_sample : constant_server_echo->second.successful_rpc_samples()) {
    timestamps.push_back(rpc_sample.start_timestamp_ns());
  }

  //Find the time intervals between the timestamps, and the maximum, minimum and avg
  std::vector<uint64_t> time_intervals;
  time_intervals.reserve(N - 1);

  uint64_t next_interval;
  double variance = 0;
  for(size_t i = 0; i < timestamps.size() - 1; i++){
    next_interval = timestamps[i + 1] - timestamps[i];
    time_intervals.push_back(next_interval);
    int64_t num = next_interval - avg_interval;
    num *= num;
    variance += static_cast<double>(num);
  }
  variance /= time_intervals.size();
  LOG(INFO) << "VARIANCE: " << variance;

  //Assert that the standard deviation is low
  ASSERT_LE(sqrt(variance), 0.2 * avg_interval);

  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);
  ASSERT_NE(instance_results_it, test_results.service_logs().instance_logs().end());

}
#endif
}  // namespace distbench
