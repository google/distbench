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

void RunConfig(const std::string& config, TestSequenceResults* results) {
  auto test_sequence = ParseTestSequenceTextProto(config);
  ASSERT_TRUE(test_sequence.ok());

  auto context = CreateContextWithDeadline(/*max_time_s=*/30);
  DistBenchTester tester;
  int num_nodes = 0;
  for (const auto& service : (*test_sequence).tests(0).services()) {
    num_nodes += service.count();
  }
  ASSERT_OK(tester.Initialize(num_nodes));
  grpc::Status status = tester.test_sequencer_stub->RunTestSequence(
      context.get(), *test_sequence, results);
  ASSERT_OK(status);
}

TEST(RpcReplayTrace, simple) {
  const std::string proto = R"(
tests {
  name: "simple"
  services {
    name: "replayer"
    count: 2
  }
  action_lists {
    name: "replayer"
    action_names: "do_replay"
  }
  actions {
    name: "do_replay"
    rpc_replay_trace_name: "single_rpc_trace"
    iterations {
      max_iteration_count: 3
      max_parallel_iterations: 2
    }
  }
  rpc_replay_traces {
    name: "single_rpc_trace"
    client: "replayer"
    server: "replayer"
    defaults {
      timing_ns: 1000000000
      timing_mode: RELATIVE_TO_TRACE_START
      server_instance: 1
      request_size: 1
      response_size: 1
    }
    records {
      client_instance: 0
    }
    records {
      client_instance: 0
    }
    records {
      timing_ns: 3000000000
      timing_mode: RELATIVE_TO_TRAFFIC_START
      client_instance: 1
      server_instance: 0
      request_size: 1
      response_size: 1
    }
    records {
      timing_ns: 0
      timing_mode: RELATIVE_TO_TRAFFIC_START
    }
  }
})";
  TestSequenceResults results;
  RunConfig(proto, &results);
  LOG(INFO) << results.DebugString();
  return;

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

}  // namespace distbench
