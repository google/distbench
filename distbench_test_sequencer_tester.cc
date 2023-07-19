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

#include "distbench_test_sequencer_tester.h"

#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"
#include "distbench_thread_support.h"
#include "glog/logging.h"

namespace distbench {

std::unique_ptr<grpc::ClientContext> CreateContextWithDeadline(int max_time_s) {
  auto context = std::make_unique<grpc::ClientContext>();
  SetGrpcClientContextDeadline(context.get(), max_time_s);
  return context;
}

DistBenchTester::~DistBenchTester() {
  TestSequence test_sequence;
  test_sequence.mutable_tests_setting()->set_shutdown_after_tests(true);
  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/10);
  grpc::Status status = test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  if (!status.ok()) {
    LOG(FATAL) << "Shutdown via RunTestSequence RPC failed " << status;
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

}  // namespace distbench
