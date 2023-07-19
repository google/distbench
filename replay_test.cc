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

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"
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

  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());
  auto maybe_results =
      tester.RunTestSequence(*test_sequence, /*timeout_s=*/30);
  ASSERT_OK(maybe_results.status());
  *results = maybe_results.value();
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
  ASSERT_EQ(results.test_results().size(), 1);
  const auto& test_results = results.test_results(0);
  const auto& instance_logs = test_results.service_logs().instance_logs();
  ASSERT_EQ(instance_logs.size(), 2);
  for (const auto& [name, log] : instance_logs) {
    LOG(INFO) << name;
    LOG(INFO) << log.DebugString();
    EXPECT_EQ(log.error_dictionary().error_message_size(), 1)
        << log.error_dictionary().DebugString();
  }
  auto it0 = instance_logs.find("replayer/0");
  ASSERT_NE(it0, instance_logs.end());
  EXPECT_EQ(it0->second.replay_trace_logs().size(), 3);
  for (const auto& trace_log : it0->second.replay_trace_logs()) {
    EXPECT_EQ(trace_log.rpc_samples_size(), 3);
    int index_mask = 0;
    for (const auto& sample : trace_log.rpc_samples()) {
      index_mask |= 1 << (sample.first);
    }
    EXPECT_EQ(index_mask, 11);
  }

  auto it1 = instance_logs.find("replayer/1");
  ASSERT_NE(it1, instance_logs.end());
  EXPECT_EQ(it1->second.replay_trace_logs().size(), 3);
  for (const auto& trace_log : it1->second.replay_trace_logs()) {
    EXPECT_EQ(trace_log.rpc_samples_size(), 2);
    int index_mask = 0;
    for (const auto& sample : trace_log.rpc_samples()) {
      index_mask |= 1 << (sample.first);
    }
    EXPECT_EQ(index_mask, 12);
  }
}

}  // namespace distbench
