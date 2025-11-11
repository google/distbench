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

#include <iostream>
#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"
#include "distbench_thread_support.h"

namespace distbench {

namespace {

std::unique_ptr<grpc::ClientContext> CreateContextWithDeadline(int max_time_s) {
  auto context = std::make_unique<grpc::ClientContext>();
  SetGrpcClientContextDeadline(context.get(), max_time_s);
  return context;
}

}  // namespace

DistBenchTester::~DistBenchTester() {
  TestSequence test_sequence;
  test_sequence.mutable_tests_setting()->set_shutdown_after_tests(true);
  TestSequenceResults results;
  auto context = CreateContextWithDeadline(/*max_time_s=*/10);
  grpc::Status status = test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  if (!status.ok()) {
    LOG(ERROR) << "Shutdown via RunTestSequence RPC failed " << status;
    test_sequencer->Shutdown();
  }
  test_sequencer->Wait();
  for (size_t i = 0; i < nodes.size(); ++i) {
    nodes[i]->Wait();
  }
  SetOverloadAbortThreshhold(0);
}

absl::Status DistBenchTester::Resize(size_t num_nodes) {
  if (!test_sequencer) {
    return absl::InvalidArgumentError("Call Initialize first");
  }
  if (num_nodes == nodes.size()) {
    return absl::OkStatus();
  }
  nodes.resize(num_nodes);
  for (size_t i = 0; i < num_nodes; ++i) {
    distbench::NodeManagerOpts nm_opts = {};
    int port = 0;
    // Flip the order of the last few nodes:
    if (i >= 2) {
      nm_opts.preassigned_node_id = num_nodes - i + 1;
    }
    nm_opts.port = &port;
    nm_opts.test_sequencer_service_address = test_sequencer->service_address();
    nm_opts.control_plane_device = "lo";
    nm_opts.service_hostname = "localhost";
    nodes[i] = std::make_unique<NodeManager>();
    auto ret = nodes[i]->Initialize(nm_opts);
    if (!ret.ok()) return ret;
  }
  return absl::OkStatus();
}

absl::Status DistBenchTester::Initialize() {
  test_sequencer = std::make_unique<TestSequencer>();
  distbench::TestSequencerOpts ts_opts = {};
  int port = 0;
  ts_opts.port = &port;
  test_sequencer->Initialize(ts_opts);
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

absl::StatusOr<TestSequenceResults> DistBenchTester::RunTestSequence(
    TestSequence test_sequence, int timeout_s) {
  int max_required_nodes = 0;
  for (const auto& test : test_sequence.tests()) {
    int required_nodes = 0;
    int num_bundles = test.node_service_bundles_size();
    int num_bundled_services = 0;
    for (const auto& service : test.services()) {
      required_nodes += service.count();
    }
    for (const auto& node_bundle : test.node_service_bundles()) {
      num_bundled_services += node_bundle.second.services_size();
    }
    required_nodes = required_nodes - num_bundled_services + num_bundles;
    if (required_nodes > max_required_nodes) {
      max_required_nodes = required_nodes;
    }
  }
  absl::Status resize_status = Resize(max_required_nodes);
  if (!resize_status.ok()) {
    return resize_status;
  }
  auto context = distbench::CreateContextWithDeadline(timeout_s);
  TestSequenceResults results;
  grpc::Status rpc_status = test_sequencer_stub->RunTestSequence(
      context.get(), test_sequence, &results);
  if (!rpc_status.ok()) {
    return grpcStatusToAbslStatus(rpc_status);
  }

  return results;
}

absl::StatusOr<TestSequenceResults>
DistBenchTester::RunTestSequenceOnSingleNodeManager(TestSequence test_sequence,
                                                    int timeout_s) {
  auto input_sequence = test_sequence;
  test_sequence.clear_tests();
  for (auto test : input_sequence.tests()) {
    if (!test.node_service_bundles().empty()) {
      std::cout << "WARNING: node_service_bundles will be overwritten\n";
    }
    test.clear_node_service_bundles();
    auto& bundles = *test.mutable_node_service_bundles();
    bundles["node0"] = AllServiceInstances(test);
    *test_sequence.add_tests() = std::move(test);
  }
  return RunTestSequence(test_sequence, timeout_s);
}

}  // namespace distbench
