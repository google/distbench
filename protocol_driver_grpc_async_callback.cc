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

#include "protocol_driver_grpc_async_callback.h"

#include "distbench_utils.h"
#include "glog/logging.h"

namespace distbench {

// Client =====================================================================
ProtocolDriverClientGrpcAsyncCallback::ProtocolDriverClientGrpcAsyncCallback()
{
}

ProtocolDriverClientGrpcAsyncCallback::~ProtocolDriverClientGrpcAsyncCallback()
{
  ShutdownClient();
}

absl::Status ProtocolDriverClientGrpcAsyncCallback::InitializeClient(
    const ProtocolDriverOptions &pd_opts) {
  return absl::OkStatus();
}

void ProtocolDriverClientGrpcAsyncCallback::SetNumPeers(int num_peers) {
  grpc_client_stubs_.resize(num_peers);
}

absl::Status ProtocolDriverClientGrpcAsyncCallback::HandleConnect(
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

std::vector<TransportStat>
    ProtocolDriverClientGrpcAsyncCallback::GetTransportStats() {
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

void ProtocolDriverClientGrpcAsyncCallback::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  CHECK_GE(peer_index, 0);
  CHECK_LT(static_cast<size_t>(peer_index), grpc_client_stubs_.size());

  ++pending_rpcs_;
  PendingRpc* new_rpc = new PendingRpc;
  new_rpc->done_callback = done_callback;
  new_rpc->state = state;
  new_rpc->request = std::move(state->request);

  auto callback_fct = [this, new_rpc,
                       done_callback](const grpc::Status& status) {
    new_rpc->status = status;
    new_rpc->state->success = status.ok();
    if (new_rpc->state->success) {
      new_rpc->state->request = std::move(new_rpc->request);
      new_rpc->state->response = std::move(new_rpc->response);
    } else {
      LOG_EVERY_N(ERROR, 1000) << "RPC failed with status: " << status;
    }
    new_rpc->done_callback();

    // Free before allowing the shutdown of the client
    delete new_rpc;
    --pending_rpcs_;
  };

  grpc_client_stubs_[peer_index]->experimental_async()->GenericRpc(
      &new_rpc->context,
      &new_rpc->request,
      &new_rpc->response,
      callback_fct
    );
}

void ProtocolDriverClientGrpcAsyncCallback::ChurnConnection(int peer) {}

void ProtocolDriverClientGrpcAsyncCallback::ShutdownClient() {
  while (pending_rpcs_) {
  }
  grpc_client_stubs_.clear();
}

// Server =====================================================================
namespace {
class TrafficServiceAsync : public Traffic::ExperimentalCallbackService {
 public:
  ~TrafficServiceAsync() override {}

  void SetHandler (
      std::function<std::function<void ()> (ServerRpcState* state)> handler) {
    handler_ = handler;
  }

  grpc::ServerUnaryReactor* GenericRpc(
      grpc::CallbackServerContext* context,
      const GenericRequest* request,
      GenericResponse* response) override {
    auto* reactor = context->DefaultReactor();
    ServerRpcState* rpc_state = new ServerRpcState;
    rpc_state->request = request;
    rpc_state->send_response = [=]() {
      *response = std::move(rpc_state->response);
      reactor->Finish(grpc::Status::OK);
    };
    rpc_state->free_state = [=]() {
      delete rpc_state;
    };
    auto fct_action_list_thread = handler_(rpc_state);
    if (fct_action_list_thread)
      RunRegisteredThread(
        "DedicatedActionListThread",
        fct_action_list_thread
      ).detach();
    return reactor;
  }

 private:
  std::function<std::function<void ()> (ServerRpcState* state)> handler_;
};
}  // anonymous namespace

ProtocolDriverServerGrpcAsyncCallback::ProtocolDriverServerGrpcAsyncCallback() {
}
ProtocolDriverServerGrpcAsyncCallback::~ProtocolDriverServerGrpcAsyncCallback()
{}

absl::Status ProtocolDriverServerGrpcAsyncCallback::InitializeServer(
    const ProtocolDriverOptions &pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  auto maybe_ip = IpAddressForDevice(netdev_name);
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  traffic_service_ = absl::make_unique<TrafficServiceAsync>();
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
    return absl::UnknownError(
        "Grpc Async Callback Traffic service failed to start");
  }

  LOG(INFO) << "Grpc Async Callback Traffic server listening on "
            << server_socket_address_;
  return absl::OkStatus();
}

void ProtocolDriverServerGrpcAsyncCallback::SetHandler(
    std::function<std::function<void ()> (ServerRpcState* state)> handler) {
  static_cast<TrafficServiceAsync*>(traffic_service_.get())
      ->SetHandler(handler);
}

absl::StatusOr<std::string>
    ProtocolDriverServerGrpcAsyncCallback::HandlePreConnect(
      std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_.ip());
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

void ProtocolDriverServerGrpcAsyncCallback::HandleConnectFailure(
    std::string_view local_connection_info) {
}

void ProtocolDriverServerGrpcAsyncCallback::ShutdownServer() {
  if (server_ != nullptr)
    server_->Shutdown();
}

std::vector<TransportStat>
    ProtocolDriverServerGrpcAsyncCallback::GetTransportStats() {
  return {};
}

// Client/Server ProtocolDriver ===============================================
ProtocolDriverGrpcAsyncCallback::ProtocolDriverGrpcAsyncCallback() {
}

absl::Status ProtocolDriverGrpcAsyncCallback::Initialize(
    const ProtocolDriverOptions &pd_opts, int* port) {

  std::string client_type = GetNamedSettingString(pd_opts.client_settings(),
                                                  "client_type",
                                                  "callback");
  if (client_type != "callback")
    return absl::InvalidArgumentError(
        "AsyncCallback is deprecated use the grpc protocol driver to specify"
        " the client type");

  std::string server_type = GetNamedSettingString(pd_opts.server_settings(),
                                                  "server_type",
                                                  "handoff");
  if (server_type != "handoff")
    return absl::InvalidArgumentError(
        "AsyncCallback is deprecated use the grpc protocol driver to specify"
        " the server type");

  absl::Status ret;
  ret = InitializeClient(pd_opts);
  if (!ret.ok())
    return ret;
  ret = InitializeServer(pd_opts, port);
  if (!ret.ok())
    return ret;

  return absl::OkStatus();
}

absl::Status ProtocolDriverGrpcAsyncCallback::InitializeClient(
    const ProtocolDriverOptions &pd_opts) {
  client_ = std::unique_ptr<ProtocolDriverClient>(
      new ProtocolDriverClientGrpcAsyncCallback());
  return client_->InitializeClient(pd_opts);
}

absl::Status ProtocolDriverGrpcAsyncCallback::InitializeServer(
    const ProtocolDriverOptions &pd_opts, int *port) {
  server_ = std::unique_ptr<ProtocolDriverServer>(
      new ProtocolDriverServerGrpcAsyncCallback());
  return server_->InitializeServer(pd_opts, port);
}

void ProtocolDriverGrpcAsyncCallback::SetHandler(
    std::function<std::function<void ()> (ServerRpcState* state)> handler) {
  server_->SetHandler(handler);
}

void ProtocolDriverGrpcAsyncCallback::SetNumPeers(int num_peers) {
  client_->SetNumPeers(num_peers);
}

ProtocolDriverGrpcAsyncCallback::~ProtocolDriverGrpcAsyncCallback() {
  ShutdownServer();
}

absl::StatusOr<std::string> ProtocolDriverGrpcAsyncCallback::HandlePreConnect(
      std::string_view remote_connection_info, int peer) {
  return server_->HandlePreConnect(remote_connection_info, peer);
}

absl::Status ProtocolDriverGrpcAsyncCallback::HandleConnect(
    std::string remote_connection_info, int peer) {
  return client_->HandleConnect(remote_connection_info, peer);
}

void ProtocolDriverGrpcAsyncCallback::HandleConnectFailure(
    std::string_view local_connection_info) {
  server_->HandleConnectFailure(local_connection_info);
}

std::vector<TransportStat> ProtocolDriverGrpcAsyncCallback::GetTransportStats()
{
  std::vector <TransportStat> stats = client_->GetTransportStats();
  std::vector <TransportStat> stats_server = server_->GetTransportStats();
  std::move(stats_server.begin(), stats_server.end(),
            std::back_inserter(stats));
  return stats;
}

void ProtocolDriverGrpcAsyncCallback::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  client_->InitiateRpc(peer_index, state, done_callback);
}

void ProtocolDriverGrpcAsyncCallback::ChurnConnection(int peer) {
  client_->ChurnConnection(peer);
}

void ProtocolDriverGrpcAsyncCallback::ShutdownClient() {
  if (client_ != nullptr)
    client_->ShutdownClient();
}

void ProtocolDriverGrpcAsyncCallback::ShutdownServer() {
  if (server_ != nullptr)
    server_->ShutdownServer();
}

}  // namespace distbench
