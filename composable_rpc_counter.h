// Copyright 2022 Google LLC
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

#ifndef DISTBENCH_PROTOCOL_DRIVER_TEST_FIXTURE_H_
#define DISTBENCH_PROTOCOL_DRIVER_TEST_FIXTURE_H_

#include "protocol_driver.h"

namespace distbench {

class ComposableRpcCounter : public ProtocolDriver {
 public:
  ComposableRpcCounter(int tree_depth);
  ~ComposableRpcCounter() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
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
  std::unique_ptr<distbench::ProtocolDriver> pd_instance_;
  int tree_depth_;
  std::atomic<int64_t> client_rpc_cnt_{0};
  std::atomic<int64_t> server_rpc_cnt_{0};
};

}  // namespace distbench

#endif  // DISTBENCH_PROTOCOL_DRIVER_TEST_FIXTURE_H_
