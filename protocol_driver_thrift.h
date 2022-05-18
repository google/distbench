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

#ifndef DISTBENCH_PROTOCOL_DRIVER_THRIFT_H_
#define DISTBENCH_PROTOCOL_DRIVER_THRIFT_H_

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>

#include "Distbench.h"
#include "distbench_utils.h"
#include "protocol_driver.h"

namespace distbench {

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace ::thrift;

class DistbenchThriftHandler : virtual public DistbenchIf {
 public:
  DistbenchThriftHandler() {}

  void GenericRPC(std::string& _return, const std::string& generic_request);

  void SetHandler(std::function<void(ServerRpcState* state)> handler);

 private:
  std::function<void(ServerRpcState* state)> handler_;
};

class TrafficService;

class ThriftPeerClient {
 public:
  ThriftPeerClient() = default;
  ThriftPeerClient(const ThriftPeerClient& value) = default;
  ThriftPeerClient(ThriftPeerClient&& value) = default;
  ~ThriftPeerClient();

  absl::Status HandleConnect(std::string ip_address, int port);
  std::unique_ptr<DistbenchClient> client_;

 private:
  std::shared_ptr<TSocket> socket_;
  std::shared_ptr<TTransport> transport_;
  std::shared_ptr<TProtocol> protocol_;
};

class ProtocolDriverThrift : public ProtocolDriver {
 public:
  ProtocolDriverThrift();
  ~ProtocolDriverThrift() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;

  void SetHandler(std::function<void(ServerRpcState* state)> handler) override;
  void SetNumPeers(int num_peers) override;

  // Connects to the actual Thrift service.
  absl::Status HandleConnect(std::string remote_connection_info,
                             int peer) override;

  // Returns the address of the Thrift service.
  absl::StatusOr<std::string> HandlePreConnect(
      std::string_view remote_connection_info, int peer) override;

  std::vector<TransportStat> GetTransportStats() override;
  void InitiateRpc(int peer_index, ClientRpcState* state,
                   std::function<void(void)> done_callback) override;
  void ChurnConnection(int peer) override;
  void ShutdownServer() override;
  void ShutdownClient() override;

 private:
  std::atomic<int> pending_rpcs_ = 0;
  std::vector<ThriftPeerClient> thrift_peer_clients_;
  int server_port_ = 0;
  DeviceIpAddress server_ip_address_;
  std::string server_socket_address_;

  std::thread server_thread_;

  std::shared_ptr<DistbenchThriftHandler> thrift_handler_;
  std::shared_ptr<TProcessor> thrift_processor_;
  std::shared_ptr<TServerTransport> thrift_serverTransport_;
  std::shared_ptr<TTransportFactory> thrift_transportFactory_;
  std::shared_ptr<TProtocolFactory> thrift_protocolFactory_;

  std::unique_ptr<TThreadedServer> thrift_server_;

  mutable absl::Mutex mutex_server_;
  bool server_initialized_ ABSL_GUARDED_BY(mutex_server_) = false;
};

}  // namespace distbench

#endif  // DISTBENCH_PROTOCOL_DRIVER_THRIFT_H_
