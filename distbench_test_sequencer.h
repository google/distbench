// Copyright 2021 Google LLC
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

#ifndef DISTBENCH_DISTBENCH_TEST_SEQUENCER_H_
#define DISTBENCH_DISTBENCH_TEST_SEQUENCER_H_

#include <set>

#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "distbench.grpc.pb.h"
#include "distbench_utils.h"

namespace distbench {

struct RegisteredNode {
  NodeRegistration registration;
  std::unique_ptr<DistBenchNodeManager::Stub> stub;
  std::string node_alias;
  bool idle = true;
};

struct TestSequencerOpts {
  std::string control_plane_device;
  int* port;
};

class TestSequencer final : public DistBenchTestSequencer::Service {
 public:
  ~TestSequencer() override;
  void Initialize(const TestSequencerOpts& opts);
  const TestSequencerOpts& GetOpts() { return opts_; }
  void Shutdown();
  void Wait();
  const std::string& service_address() { return service_address_; }

  grpc::Status RegisterNode(grpc::ServerContext* context,
                            const NodeRegistration* request,
                            NodeConfig* response) override;

  grpc::Status RunTestSequence(grpc::ServerContext* context,
                               const TestSequence* request,
                               TestSequenceResults* response) override;

 private:
  grpc::Status DoRunTestSequence(grpc::ServerContext* context,
                                 const TestSequence* request,
                                 TestSequenceResults* response);

  absl::StatusOr<TestResult> DoRunTest(
      grpc::ServerContext* context, const DistributedSystemDescription& test);

  absl::StatusOr<std::map<std::string, std::set<std::string>>> PlaceServices(
      const DistributedSystemDescription& test);

  absl::StatusOr<ServiceEndpointMap> ConfigureNodes(
      const std::map<std::string, std::set<std::string>>& node_service_map,
      const DistributedSystemDescription& test);

  absl::Status IntroducePeers(
      const std::map<std::string, std::set<std::string>>& node_service_map,
      ServiceEndpointMap service_map);

  absl::StatusOr<GetTrafficResultResponse> RunTraffic(
      const std::map<std::string, std::set<std::string>>& node_service_map,
      int64_t timeout_seconds);

  void CancelTraffic() ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Mutex mutex_;
  std::vector<RegisteredNode> registered_nodes_ ABSL_GUARDED_BY(mutex_);
  std::map<std::string, int> node_alias_id_map_ ABSL_GUARDED_BY(mutex_);
  std::map<std::string, int> node_registration_id_map_ ABSL_GUARDED_BY(mutex_);
  grpc::ServerContext* running_test_sequence_context_ ABSL_GUARDED_BY(mutex_) =
      nullptr;
  std::shared_ptr<absl::Notification> running_test_notification_
      ABSL_GUARDED_BY(mutex_);
  std::unique_ptr<grpc::Server> grpc_server_;
  std::string service_address_;
  TestSequencerOpts opts_;
  SafeNotification shutdown_requested_;
};

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_TEST_SEQUENCER_H_
