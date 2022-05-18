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

#include "protocol_driver_thrift.h"

#include <glog/logging.h>
#include <sys/types.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>
#include <unistd.h>

#include "Distbench.h"
#include "distbench_utils.h"

namespace distbench {

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

absl::Status ThriftPeerClient::HandleConnect(std::string ip_address, int port) {
  socket_ = std::make_shared<TSocket>(ip_address, port);
  transport_ = std::make_shared<TBufferedTransport>(socket_);
  protocol_ = std::make_shared<TBinaryProtocol>(transport_);
  client_ = std::make_unique<DistbenchClient>(protocol_);

  try {
    transport_->open();
  } catch (TException& tx) {
    return absl::UnknownError(tx.what());
  }

  return absl::OkStatus();
}

ThriftPeerClient::~ThriftPeerClient() {
  client_.reset();
  protocol_.reset();
  transport_.reset();
  socket_.reset();
}

void DistbenchThriftHandler::GenericRPC(std::string& _return,
                                        const std::string& payload) {
  ServerRpcState* rpc_state = new ServerRpcState;

  distbench::GenericRequest* request = new distbench::GenericRequest();
  bool success = request->ParseFromString(payload);
  if (!success) {
    LOG(ERROR) << "Unable to decode payload of received GenericRPC (Thrift) !";
  }
  rpc_state->request = request;
  rpc_state->have_dedicated_thread = true;
  rpc_state->send_response = [&]() {
    rpc_state->response.SerializeToString(&_return);
  };
  rpc_state->free_state = [=]() { delete rpc_state; };
  handler_(rpc_state);
}

void DistbenchThriftHandler::SetHandler(
    std::function<void(ServerRpcState* state)> handler) {
  handler_ = handler;
}

ProtocolDriverThrift::ProtocolDriverThrift() {}

absl::Status ProtocolDriverThrift::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  absl::MutexLock m(&mutex_server_);

  if (server_initialized_) {
    LOG(ERROR) << "Server already initialized !";
    return absl::UnknownError("Thrift server already initialized !");
  }

  std::string netdev_name = pd_opts.netdev_name();
  auto maybe_ip = IpAddressForDevice(netdev_name);
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();

  thrift_handler_ = std::make_unique<DistbenchThriftHandler>();
  thrift_processor_ = std::make_unique<DistbenchProcessor>(thrift_handler_);
  TServerSocket* socket = new TServerSocket(server_ip_address_.ip(), *port);
  thrift_serverTransport_ = std::shared_ptr<TServerTransport>(socket);
  thrift_transportFactory_ = std::make_unique<TBufferedTransportFactory>();
  thrift_protocolFactory_ = std::make_unique<TBinaryProtocolFactory>();
  thrift_server_ = std::make_unique<TThreadedServer>(
      thrift_processor_, thrift_serverTransport_, thrift_transportFactory_,
      thrift_protocolFactory_);
  server_thread_ = std::thread{[&]() {
    thrift_server_->serve();
    LOG(INFO) << "thrift_server_ stopped serving";
  }};

  while (!socket->isOpen())
    ;
  *port = socket->getPort();
  server_port_ = *port;
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);

  server_initialized_ = true;
  LOG(INFO) << "Thrift server listening on " << server_socket_address_;
  return absl::OkStatus();
}

void ProtocolDriverThrift::SetHandler(
    std::function<void(ServerRpcState* state)> handler) {
  thrift_handler_->SetHandler(handler);
}

void ProtocolDriverThrift::SetNumPeers(int num_peers) {
  thrift_peer_clients_.resize(num_peers);
}

ProtocolDriverThrift::~ProtocolDriverThrift() {
  ShutdownServer();
  thrift_peer_clients_.clear();
}

absl::StatusOr<std::string> ProtocolDriverThrift::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_.ip());
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

absl::Status ProtocolDriverThrift::HandleConnect(
    std::string remote_connection_info, int peer) {
  CHECK_GE(peer, 0);
  CHECK_LT(static_cast<size_t>(peer), thrift_peer_clients_.size());
  ServerAddress addr;
  addr.ParseFromString(remote_connection_info);
  LOG(INFO) << "Thrift HandleConnect to " << addr.socket_address();

  return thrift_peer_clients_[peer].HandleConnect(addr.ip_address(),
                                                  addr.port());
}

std::vector<TransportStat> ProtocolDriverThrift::GetTransportStats() {
  return {};
}

namespace {
struct PendingRpc {
  GenericRequest request;
  GenericResponse response;
  std::function<void(void)> done_callback;
  ClientRpcState* state;
};
}  // anonymous namespace

void ProtocolDriverThrift::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  CHECK_GE(peer_index, 0);
  CHECK_LT(static_cast<size_t>(peer_index), thrift_peer_clients_.size());

  ++pending_rpcs_;

  PendingRpc* new_rpc = new PendingRpc;
  new_rpc->done_callback = done_callback;
  new_rpc->state = state;
  new_rpc->request = std::move(state->request);

  std::string request_encoded;
  new_rpc->request.SerializeToString(&request_encoded);

  std::string response_encoded;
  thrift_peer_clients_[peer_index].client_->GenericRPC(response_encoded,
                                                       request_encoded);
  bool success = new_rpc->response.ParseFromString(response_encoded);
  if (!success) {
    LOG(ERROR) << "Unable to decode payload";
  } else {
    new_rpc->state->request = std::move(new_rpc->request);
    new_rpc->state->response = std::move(new_rpc->response);
  }
  new_rpc->state->success = success;
  new_rpc->done_callback();

  delete new_rpc;
  --pending_rpcs_;
}

void ProtocolDriverThrift::ChurnConnection(int peer) {}

void ProtocolDriverThrift::ShutdownClient() {
  while (pending_rpcs_) {
  }
}

void ProtocolDriverThrift::ShutdownServer() {
  absl::MutexLock m(&mutex_server_);

  if (!server_initialized_) {
    LOG(INFO) << "ShutdownServer called while uninitialized !";
    return;
  }

  server_initialized_ = false;

  thrift_server_->stop();

  // Wait for thrift_server_ to shutdown
  server_thread_.join();
}

}  // namespace distbench
