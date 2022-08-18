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

#ifndef DISTBENCH_PROTOCOL_DRIVER_DOUBLE_BARREL_H_
#define DISTBENCH_PROTOCOL_DRIVER_DOUBLE_BARREL_H_

#include "distbench.grpc.pb.h"
#include "distbench_utils.h"
#include "protocol_driver.h"
#include "distbench_threadpool.h"

namespace distbench {

class ProtocolDriverDoubleBarrel : public ProtocolDriver {
 public:
  ProtocolDriverDoubleBarrel(int tree_depth);
  ~ProtocolDriverDoubleBarrel() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;
  absl::Status InitializeClient(const ProtocolDriverOptions& pd_opts) override;
  absl::Status InitializeServer(const ProtocolDriverOptions& pd_opts,
                                int* port) override;

  void SetHandler(std::function<std::function<void()>(ServerRpcState* state)>
                      handler) override;
  void SetNumPeers(int num_peers) override;

  // Connects to the actual GRPC service.
  absl::Status HandleConnect(std::string remote_connection_info,
                             int peer) override;

  // Returns the address of the GRPC service.
  absl::StatusOr<std::string> HandlePreConnect(
      std::string_view remote_connection_info, int peer) override;
  void HandleConnectFailure(std::string_view local_connection_info) override;

  std::vector<TransportStat> GetTransportStats() override;
  void InitiateRpc(int peer_index, ClientRpcState* state,
                   std::function<void(void)> done_callback) override;
  void ChurnConnection(int peer) override;
  void ShutdownServer() override;
  void ShutdownClient() override;

 private:
  std::unique_ptr<distbench::ProtocolDriver> barrel_1_, barrel_2_;
  int port_1_ = 0, port_2_ = 0, tree_depth_;
  bool use_barrel_1_ = true;
};

}  // namespace distbench

#endif  // DISTBENCH_PROTOCOL_DRIVER_DOUBLE_BARREL_H_
