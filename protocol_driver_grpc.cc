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

// Client =====================================================================
ProtocolDriverClientGrpc::ProtocolDriverClientGrpc() {}
ProtocolDriverClientGrpc::~ProtocolDriverClientGrpc() {
  ShutdownClient();
}

absl::Status ProtocolDriverClientGrpc::Initialize(
    const ProtocolDriverOptions &pd_opts) {
  cq_poller_ = std::thread(&ProtocolDriverClientGrpc::RpcCompletionThread,
                           this);
  return absl::OkStatus();
}

void ProtocolDriverClientGrpc::SetNumPeers(int num_peers) {
  grpc_client_stubs_.resize(num_peers);
}

absl::Status ProtocolDriverClientGrpc::HandleConnect(
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

std::vector<TransportStat> ProtocolDriverClientGrpc::GetTransportStats() {
  return {};
}

namespace {
struct PendingRpc {
  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientAsyncResponseReader<GenericResponse>> rpc;
  grpc::Status status;
  GenericRequest request;
  GenericResponse response;
  std::function<void(void)> done_callback;
  ClientRpcState* state;
};
}  // anonymous namespace

void ProtocolDriverClientGrpc::InitiateRpc(
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

void ProtocolDriverClientGrpc::RpcCompletionThread() {
  while (!shutdown_.HasBeenNotified()) {
    bool ok;
    void* tag;
    tag = nullptr;
    ok = false;
    cq_.Next(&tag, &ok);
    if (ok) {
      PendingRpc *finished_rpc = static_cast<PendingRpc*>(tag);
      finished_rpc->state->success = finished_rpc->status.ok();
      if (finished_rpc->state->success) {
        finished_rpc->state->request = std::move(finished_rpc->request);
        finished_rpc->state->response = std::move(finished_rpc->response);
      } else {
        LOG_EVERY_N(ERROR, 1000)
            << "RPC failed with status: " << finished_rpc->status;
      }
      finished_rpc->done_callback();
      --pending_rpcs_;
      delete finished_rpc;
    }
  }
}

void ProtocolDriverClientGrpc::ChurnConnection(int peer) {}

void ProtocolDriverClientGrpc::ShutdownClient() {
  while (pending_rpcs_) {
  }
  if (!shutdown_.HasBeenNotified()) {
    shutdown_.Notify();
    cq_.Shutdown();
    if (cq_poller_.joinable()) {
      cq_poller_.join();
    }
  }
  grpc_client_stubs_.clear();
}

// Server =====================================================================
namespace {
class TrafficService : public Traffic::Service {
 public:
  ~TrafficService() override {}

  void SetHandler (
      std::function<std::function<void ()> (ServerRpcState* state)> handler
      ) {
    handler_ = handler;
  }

  grpc::Status GenericRpc(
      grpc::ServerContext* context,
      const GenericRequest* request,
      GenericResponse* response) override {
    ServerRpcState rpc_state;
    rpc_state.have_dedicated_thread = true;
    rpc_state.request = request;
    rpc_state.send_response = [&]() {
      *response = std::move(rpc_state.response);
    };
    handler_(&rpc_state);
    // Note: this should be an asynchronous server for generality.
    return grpc::Status::OK;
  }

 private:
  std::function<std::function<void ()> (ServerRpcState* state)> handler_;
};

}  // anonymous namespace
ProtocolDriverServerGrpc::ProtocolDriverServerGrpc() {}
ProtocolDriverServerGrpc::~ProtocolDriverServerGrpc() {}

absl::Status ProtocolDriverServerGrpc::Initialize(
    const ProtocolDriverOptions &pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  auto maybe_ip = IpAddressForDevice(netdev_name);
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  traffic_service_ = absl::make_unique<TrafficService>();
  grpc::ServerBuilder builder;
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> server_creds =
    MakeServerCredentials();
  builder.AddListeningPort(server_socket_address_, server_creds, port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  ApplyServerSettingsToGrpcBuilder(&builder, pd_opts);
  builder.RegisterService(traffic_service_.get());
  server_ = builder.BuildAndStart();

  server_port_ = *port;
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  if (!server_) {
    return absl::UnknownError("Grpc Traffic service failed to start");
  }

  LOG(INFO) << "Grpc Traffic server listening on " << server_socket_address_;

  return absl::OkStatus();
}

void ProtocolDriverServerGrpc::SetHandler(
    std::function<std::function<void ()> (ServerRpcState* state)> handler
    ) {
  static_cast<TrafficService*>(traffic_service_.get())->SetHandler(handler);
}

absl::StatusOr<std::string> ProtocolDriverServerGrpc::HandlePreConnect(
      std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_.ip());
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

void ProtocolDriverServerGrpc::HandleConnectFailure(
    std::string_view local_connection_info) {
}

void ProtocolDriverServerGrpc::ShutdownServer() {
  server_->Shutdown();
}

std::vector<TransportStat> ProtocolDriverServerGrpc::GetTransportStats() {
  return {};
}

// Client/Server ProtocolDriver ===============================================
ProtocolDriverGrpc::ProtocolDriverGrpc() {
}

absl::Status ProtocolDriverGrpc::Initialize(
    const ProtocolDriverOptions &pd_opts, int* port) {
  absl::Status ret;

  // Build the client
  client_ = std::unique_ptr<ProtocolDriverClient>(
      new ProtocolDriverClientGrpc());
  ret = client_->Initialize(pd_opts);
  if (!ret.ok())
    return ret;

  // Build the server
  server_ = std::unique_ptr<ProtocolDriverServer>(
      new ProtocolDriverServerGrpc());
  ret = server_->Initialize(pd_opts, port);
  if (!ret.ok())
    return ret;

  return absl::OkStatus();
}

void ProtocolDriverGrpc::SetHandler(
    std::function<std::function<void ()> (ServerRpcState* state)> handler) {
  server_->SetHandler(handler);
}

void ProtocolDriverGrpc::SetNumPeers(int num_peers) {
  client_->SetNumPeers(num_peers);
}

ProtocolDriverGrpc::~ProtocolDriverGrpc() {
  ShutdownServer();
}

absl::StatusOr<std::string> ProtocolDriverGrpc::HandlePreConnect(
      std::string_view remote_connection_info, int peer) {
  return server_->HandlePreConnect(remote_connection_info, peer);
}

absl::Status ProtocolDriverGrpc::HandleConnect(
    std::string remote_connection_info, int peer) {
  return client_->HandleConnect(remote_connection_info, peer);
}

void ProtocolDriverGrpc::HandleConnectFailure(
    std::string_view local_connection_info) {
  server_->HandleConnectFailure(local_connection_info);
}

std::vector<TransportStat> ProtocolDriverGrpc::GetTransportStats() {
  std::vector <TransportStat> stats = client_->GetTransportStats();
  std::vector <TransportStat> stats_server = server_->GetTransportStats();
  std::move(stats_server.begin(), stats_server.end(),
            std::back_inserter(stats));
  return stats;
}

void ProtocolDriverGrpc::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  client_->InitiateRpc(peer_index, state, done_callback);
}

void ProtocolDriverGrpc::ChurnConnection(int peer) {
  client_->ChurnConnection(peer);
}

void ProtocolDriverGrpc::ShutdownClient() {
  if (client_ != nullptr)
    client_->ShutdownClient();
}

void ProtocolDriverGrpc::ShutdownServer() {
  if (server_ != nullptr)
    server_->ShutdownServer();
}

}  // namespace distbench
