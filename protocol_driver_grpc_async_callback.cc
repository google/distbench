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

namespace {

class TrafficServiceAsync : public Traffic::ExperimentalCallbackService {
 public:
  ~TrafficServiceAsync() override {}

  void SetHandler (std::function<void(ServerRpcState* state)> handler) {
    handler_ = handler;
  }

  grpc::ServerUnaryReactor* GenericRpc(
      grpc::CallbackServerContext* context,
      const GenericRequest* request,
      GenericResponse* response) override {
    ServerRpcState rpc_state;
    rpc_state.request = request;
    rpc_state.send_response = [&]() {
      *response = std::move(rpc_state.response);
    };
    if (handler_)
      handler_(&rpc_state);

    // Return reactor
    auto* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
  }

 private:
  std::function<void(ServerRpcState* state)> handler_;
};

}  // anonymous namespace

ProtocolDriverGrpcAsyncCallback::ProtocolDriverGrpcAsyncCallback() {
}

absl::Status ProtocolDriverGrpcAsyncCallback::Initialize(
    std::string_view netdev_name, int* port) {
  server_ip_address_ = IpAddressForDevice("");
  server_socket_address_ = SocketAddressForDevice("", *port);
  traffic_service_ = absl::make_unique<TrafficServiceAsync>();
  grpc::ServerBuilder builder;
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> server_creds =
    MakeServerCredentials();
  builder.AddListeningPort(server_socket_address_, server_creds, port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(traffic_service_.get());
  server_ = builder.BuildAndStart();
  server_port_ = *port;
  server_socket_address_ = SocketAddressForDevice("", *port);
  if (server_) {
    LOG(INFO) << "Grpc Async Callback Traffic server listening on "
              << server_socket_address_;
    return absl::OkStatus();
  } else {
    return absl::UnknownError(
        "Grpc Async Callback Traffic service failed to start");
  }
}

void ProtocolDriverGrpcAsyncCallback::SetHandler(
    std::function<void(ServerRpcState* state)> handler) {
  static_cast<TrafficServiceAsync*>(traffic_service_.get())->SetHandler(handler);
}

void ProtocolDriverGrpcAsyncCallback::SetNumPeers(int num_peers) {
  grpc_client_stubs_.resize(num_peers);
}

ProtocolDriverGrpcAsyncCallback::~ProtocolDriverGrpcAsyncCallback() {
  ShutdownServer();
  grpc_client_stubs_.clear();
}

absl::StatusOr<std::string> ProtocolDriverGrpcAsyncCallback::HandlePreConnect(
      std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_);
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

absl::Status ProtocolDriverGrpcAsyncCallback::HandleConnect(
    std::string remote_connection_info, int peer) {
  CHECK_GE(peer, 0);
  CHECK_LT(static_cast<size_t>(peer), grpc_client_stubs_.size());
  ServerAddress addr;
  addr.ParseFromString(remote_connection_info);
  LOG(INFO) << addr.DebugString();
  std::shared_ptr<grpc::ChannelCredentials> creds =
    MakeChannelCredentials();
  std::shared_ptr<grpc::Channel> channel =
    grpc::CreateCustomChannel(addr.socket_address(), creds,
                              GetDefaultChannelArguments());
  grpc_client_stubs_[peer] = Traffic::NewStub(channel);
  return absl::OkStatus();
}

std::vector<TransportStat> ProtocolDriverGrpcAsyncCallback::GetTransportStats() {
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

void ProtocolDriverGrpcAsyncCallback::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  CHECK_GE(peer_index, 0);
  CHECK_LT(static_cast<size_t>(peer_index), grpc_client_stubs_.size());

  ++pending_rpcs_;
  PendingRpc* new_rpc = new PendingRpc;
  new_rpc->done_callback = done_callback;
  new_rpc->state = state;
  new_rpc->request = std::move(state->request);

  auto callback_fct = [this, new_rpc, done_callback](const grpc::Status& status) {
    if (!status.ok()) {
      LOG(ERROR) << new_rpc->status;
      new_rpc->state->success = false;
    } else {
      new_rpc->state->success = true;
      new_rpc->state->request = std::move(new_rpc->request);
      new_rpc->state->response = std::move(new_rpc->response);
    }
    new_rpc->done_callback();
    --pending_rpcs_;
    delete new_rpc;
  };

  grpc_client_stubs_[peer_index]->experimental_async()->GenericRpc(
      &new_rpc->context,
      &new_rpc->request,
      &new_rpc->response,
      callback_fct
    );
}

void ProtocolDriverGrpcAsyncCallback::ChurnConnection(int peer) {}

void ProtocolDriverGrpcAsyncCallback::ShutdownClient() {
  while (pending_rpcs_) {
  }
}

void ProtocolDriverGrpcAsyncCallback::ShutdownServer() {
}

}  // namespace distbench
