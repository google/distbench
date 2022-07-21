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

#ifndef DISTBENCH_PROTOCOL_DRIVER_MERCURY_H_
#define DISTBENCH_PROTOCOL_DRIVER_MERCURY_H_

#include <mercury.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#include "distbench_utils.h"
#include "protocol_driver.h"

namespace distbench {

typedef struct {
  std::string string;
} mercury_generic_rpc_string_t;

class ProtocolDriverMercury : public ProtocolDriver {
 public:
  ProtocolDriverMercury();
  ~ProtocolDriverMercury() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;
  absl::Status InitializeServer(const ProtocolDriverOptions& pd_opts,
                                int* port) override;
  absl::Status InitializeClient(const ProtocolDriverOptions& pd_opts) override;

  void SetHandler(std::function<std::function<void()>(ServerRpcState* state)>
                      handler) override;
  void SetNumPeers(int num_peers) override;

  // Connects to the actual MERCURY service.
  absl::Status HandleConnect(std::string remote_connection_info,
                             int peer) override;

  // Returns the address of the MERCURY service.
  absl::StatusOr<std::string> HandlePreConnect(
      std::string_view remote_connection_info, int peer) override;

  std::vector<TransportStat> GetTransportStats() override;
  void InitiateRpc(int peer_index, ClientRpcState* state,
                   std::function<void(void)> done_callback) override;
  void ChurnConnection(int peer) override;
  void ShutdownServer() override;
  void ShutdownClient() override;

 private:
  void RpcCompletionThread();
  void PrintMercuryVersion();

  hg_return_t RpcClientCallback(const struct hg_cb_info* callback_info);
  hg_return_t RpcServerCallback(hg_handle_t handle);

  static hg_return_t StaticRpcServerCallback(hg_handle_t handle);
  static hg_return_t StaticRpcServerDoneCallback(
      const struct hg_cb_info* hg_cb_info);
  static hg_return_t StaticRpcServerSerialize(hg_proc_t proc, void* data);
  static hg_return_t StaticClientCallback(
      const struct hg_cb_info* callback_info);

  std::atomic<int> pending_rpcs_ = 0;
  absl::Notification shutdown_;
  std::thread progress_thread_;
  DeviceIpAddress server_ip_address_;
  std::string server_socket_address_;

  hg_context_t* hg_context_ = nullptr;
  hg_class_t* hg_class_ = nullptr;

  hg_id_t mercury_generic_rpc_id_;
  std::vector<hg_addr_t> remote_addresses_;

  std::function<std::function<void()>(ServerRpcState* state)> handler_;
};

}  // namespace distbench

#endif  // DISTBENCH_PROTOCOL_DRIVER_MERCURY_H_
