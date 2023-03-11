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

#ifndef DISTBENCH_PROTOCOL_DRIVER_H_
#define DISTBENCH_PROTOCOL_DRIVER_H_

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "distbench.pb.h"
#include "grpc_wrapper.h"
#include "simple_clock.h"

namespace distbench {

struct ClientRpcState {
  GenericRequest request;
  GenericResponse response;
  absl::Time prior_start_time = absl::InfinitePast();
  absl::Time start_time = absl::InfinitePast();
  absl::Time end_time;
  bool success;
};

struct ServerRpcState {
  const GenericRequest* request;
  GenericResponse response;
  bool have_dedicated_thread = false;

  void SetSendResponseFunction(
      std::function<void(void)> send_response_function);
  void SendResponseIfSet();

  void SetFreeStateFunction(std::function<void(void)> free_state_function);
  void FreeStateIfSet();

 private:
  std::function<void(void)> send_response_function_;
  std::function<void(void)> free_state_function_;
};

struct TransportStat {
  std::string name;
  int64_t value;
};

using RpcId = int64_t;

class ProtocolDriverClient {
 public:
  virtual ~ProtocolDriverClient() {}
  virtual absl::Status Initialize(const ProtocolDriverOptions& pd_opts) {
    return absl::OkStatus();
  }

  // Client interface =========================================================
  virtual void SetNumPeers(int num_peers) = 0;

  // Allocate local resources that are needed to establish a connection
  // E.g. an unconnected RoCE QueuePair. Returns opaque data. If no local
  // resources are needed, this is a NOP.
  virtual absl::StatusOr<std::string> Preconnect();

  // Actually establish a conection, given the opaque data from the
  // the responder. E.g. connect the local and remote RoCE queue pairs.
  virtual absl::Status HandleConnect(std::string remote_connection_info,
                                     int peer) = 0;
  virtual void InitiateRpc(int peer_index, ClientRpcState* state,
                           std::function<void(void)> done_callback) = 0;
  virtual void ChurnConnection(int peer) = 0;
  virtual void ShutdownClient() = 0;

  // Misc interface ===========================================================
  virtual std::vector<TransportStat> GetTransportStats() = 0;
};

class ProtocolDriverServer {
 public:
  virtual ~ProtocolDriverServer() {}
  virtual absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                                  int* port) {
    return absl::OkStatus();
  }

  // Server interface =========================================================
  virtual void SetHandler(
      std::function<std::function<void()>(ServerRpcState* state)> handler) = 0;
  // Return the address of a running server that a client can connect to, or
  // actually establish a single conection, given the opaque data from the
  // initiator. E.g. allocate an unconnected RoCE queue pair, and connect it
  // to the remote queue pair, and return the info about the newly allocated
  // queue pair so that the initiator can connect the queue pairs on its end.
  virtual absl::StatusOr<std::string> HandlePreConnect(
      std::string_view remote_connection_info, int peer) = 0;
  virtual void ShutdownServer() = 0;

  // Handle the remote side responding with an RPC error by cleaning up
  // the local resources associated with the opaque data.
  virtual void HandleConnectFailure(std::string_view local_connection_info);

  // Misc interface ===========================================================
  virtual std::vector<TransportStat> GetTransportStats() = 0;
};

class ProtocolDriver : public ProtocolDriverClient,
                       public ProtocolDriverServer {
 public:
  virtual ~ProtocolDriver() {}
  virtual absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                                  int* port) = 0;
  virtual std::vector<TransportStat> GetTransportStats() = 0;

  // Misc interface ===========================================================
  virtual SimpleClock& GetClock();

 private:
  // Hide the Initialize functions of the base classes:
  using ProtocolDriverClient::Initialize;
  using ProtocolDriverServer::Initialize;
};

}  // namespace distbench

#endif  //  DISTBENCH_PROTOCOL_DRIVER_H_
