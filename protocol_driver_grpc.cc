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

#include "protocol_driver_grpc.h"

#include "distbench_utils.h"
#include "glog/logging.h"

namespace distbench {

namespace {

class TrafficService : public Traffic::Service {
 public:
  ~TrafficService() override {}

  void SetHandler (std::function<void(ServerRpcState* state)> handler) {
    handler_ = handler;
  }

  grpc::Status GenericRpc(
      grpc::ServerContext* context,
      const GenericRequest* request,
      GenericResponse* response) override {
    ServerRpcState rpc_state;
    rpc_state.request = request;
    rpc_state.send_response = [&]() {
      *response = std::move(rpc_state.response);
    };
    handler_(&rpc_state);
    // Note: this should be an asynchronous server for generality.
    return grpc::Status::OK;
  }

 private:
  std::function<void(ServerRpcState* state)> handler_;
};

}  // anonymous namespace

ProtocolDriverGrpc::ProtocolDriverGrpc() {
}

absl::Status ProtocolDriverGrpc::Initialize(
    std::string_view netdev_name, int* port) {
  traffic_service_ = absl::make_unique<TrafficService>();

  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> server_creds =
    MakeServerCredentials();
  builder.AddListeningPort("[::]:0", server_creds, port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(traffic_service_.get());
  server_ = builder.BuildAndStart();

  server_ip_address_ = IpAddressForDevice(netdev_name);
  server_port_ = *port;
  server_socket_address_ = SocketAddressForDevice(netdev_name, *port);
  if (!server_) {
    return absl::UnknownError("Grpc Traffic service failed to start");
  }

  LOG(INFO) << "Grpc Traffic server listening on " << server_socket_address_;
  cq_poller_ = std::thread(&ProtocolDriverGrpc::RpcCompletionThread, this);
  return absl::OkStatus();
}

void ProtocolDriverGrpc::SetHandler(
    std::function<void(ServerRpcState* state)> handler) {
  static_cast<TrafficService*>(traffic_service_.get())->SetHandler(handler);
}

void ProtocolDriverGrpc::SetNumPeers(int num_peers) {
  grpc_client_stubs_.resize(num_peers);
}

ProtocolDriverGrpc::~ProtocolDriverGrpc() {
  ShutdownServer();
  grpc_client_stubs_.clear();
}

absl::StatusOr<std::string> ProtocolDriverGrpc::HandlePreConnect(
      std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_);
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

absl::Status ProtocolDriverGrpc::HandleConnect(
    std::string remote_connection_info, int peer) {
  CHECK_GE(peer, 0);
  CHECK_LT(static_cast<size_t>(peer), grpc_client_stubs_.size());
  ServerAddress addr;
  addr.ParseFromString(remote_connection_info);
  std::shared_ptr<grpc::ChannelCredentials> creds =
    MakeChannelCredentials();
  std::shared_ptr<grpc::Channel> channel =
    grpc::CreateCustomChannel(addr.socket_address(), creds,
                              DistbenchCustomChannelArguments());
  grpc_client_stubs_[peer] = Traffic::NewStub(channel);
  return absl::OkStatus();
}

std::vector<TransportStat> ProtocolDriverGrpc::GetTransportStats() {
  return {};
}

struct PendingRpc {
  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientAsyncResponseReader<GenericResponse>> rpc;
  grpc::Status status;
  GenericRequest request;
  GenericResponse response;
  std::function<void(void)> done_callback;
  ClientRpcState* state;
};

void ProtocolDriverGrpc::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  CHECK_GE(peer_index, 0);
  CHECK_LT(static_cast<size_t>(peer_index), grpc_client_stubs_.size());
  ++pending_rpcs_;
  PendingRpc* new_rpc = new PendingRpc;
  new_rpc->done_callback = done_callback;
  new_rpc->state = state;
  new_rpc->request = std::move(state->request);
  new_rpc->rpc = grpc_client_stubs_[peer_index]->AsyncGenericRpc(
      &new_rpc->context, new_rpc->request, &cq_);
  new_rpc->rpc->Finish(&new_rpc->response, &new_rpc->status, new_rpc);
}

void ProtocolDriverGrpc::RpcCompletionThread() {
  while (!shutdown_.HasBeenNotified()) {
    bool ok;
    void* tag;
    tag = nullptr;
    ok = false;
    cq_.Next(&tag, &ok);
    if (ok) {
      PendingRpc *finished_rpc = static_cast<PendingRpc*>(tag);
      if (!finished_rpc->status.ok()) {
        LOG(ERROR) << finished_rpc->status;
        finished_rpc->state->success = false;
      } else {
        finished_rpc->state->success = true;
        finished_rpc->state->request = std::move(finished_rpc->request);
        finished_rpc->state->response = std::move(finished_rpc->response);
      }
      finished_rpc->done_callback();
      --pending_rpcs_;
      delete finished_rpc;
    }
  }
}

void ProtocolDriverGrpc::ChurnConnection(int peer) {}

void ProtocolDriverGrpc::ShutdownClient() {
  while (pending_rpcs_) {
  }
}

void ProtocolDriverGrpc::ShutdownServer() {
  if (!shutdown_.HasBeenNotified()) {
    shutdown_.Notify();
    cq_.Shutdown();
    if (cq_poller_.joinable()) {
      cq_poller_.join();
    }
  }
}

}  // namespace distbench
