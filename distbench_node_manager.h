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

#ifndef DISTBENCH_DISTBENCH_NODE_MANAGER_H_
#define DISTBENCH_DISTBENCH_NODE_MANAGER_H_

#include "absl/status/statusor.h"
#include "distbench.grpc.pb.h"
#include "distbench_engine.h"

namespace distbench {

struct NodeManagerOpts {
  std::string test_sequencer_service_address;
  std::string default_data_plane_device;
  int* port;
};

class NodeManager final : public DistBenchNodeManager::Service {
 public:
  ~NodeManager() override;
  absl::Status Initialize(const NodeManagerOpts& opts);
  const NodeManagerOpts& GetOpts() { return opts_; }
  void Shutdown();
  void Wait();
  const std::string& service_address() { return service_address_; }

  NodeManager();

  grpc::Status ConfigureNode(grpc::ServerContext* context,
                             const NodeServiceConfig* request,
                             ServiceEndpointMap* response) override;

  grpc::Status IntroducePeers(grpc::ServerContext* context,
                              const ServiceEndpointMap* request,
                              IntroducePeersResult* response) override;

  grpc::Status RunTraffic(grpc::ServerContext* context,
                          const RunTrafficRequest* request,
                          RunTrafficResponse* response) override;

  grpc::Status GetTrafficResult(grpc::ServerContext* context,
                                const GetTrafficResultRequest* request,
                                GetTrafficResultResponse* response) override;

  grpc::Status CancelTraffic(grpc::ServerContext* context,
                             const CancelTrafficRequest* request,
                             CancelTrafficResult* response) override;

  grpc::Status ShutdownNode(grpc::ServerContext* context,
                            const ShutdownNodeRequest* request,
                            ShutdownNodeResult* response) override;

  absl::StatusOr<ProtocolDriverOptions> ResolveProtocolDriverAlias(
      const std::string& protocol_name);

 private:
  void ClearServices() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  struct ServiceOpts {
    std::string_view service_name;
    std::string_view service_type;
    int service_instance;
    int* port;
    std::string_view protocol;
    std::string_view netdev;
  };

  absl::StatusOr<ProtocolDriverOptions> GetProtocolDriverOptionsFor(
      const ServiceOpts& service_opts) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Status AllocService(const ServiceOpts& service_opts)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Mutex mutex_;
  DistributedSystemDescription traffic_config_ ABSL_GUARDED_BY(mutex_);
  ServiceEndpointMap peers_ ABSL_GUARDED_BY(mutex_);

  std::map<std::string, std::unique_ptr<DistBenchEngine>> service_engines_
      ABSL_GUARDED_BY(mutex_);

  std::unique_ptr<grpc::Server> grpc_server_;
  std::string service_address_;
  NodeManagerOpts opts_;
  absl::Notification shutdown_requested_;
  NodeConfig config_;

  struct rusage rusage_start_test_;
};

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_NODE_MANAGER_H_
