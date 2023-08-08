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

#include "protocol_driver_grpc.h"

#include <memory>

#include "absl/base/internal/sysinfo.h"
#include "distbench_thread_support.h"
#include "glog/logging.h"

#if WITH_HOMA_GRPC
#include "homa_client.h"
#include "homa_listener.h"
#endif

namespace distbench {

namespace {

const char* kDefaultTransport = "tcp";

std::string AddGrpcProtocol(std::string_view s) {
  std::string ret;
  if (!s.empty()) {
    if (s[0] == '[') {
      ret = "ipv6:";
    } else {
      ret = "ipv4:";
    }
    ret += s;
  }
  return ret;
}

absl::StatusOr<std::shared_ptr<grpc::Channel>> CreateClientChannel(
    const std::string& socket_address, std::string_view transport) {
  if (transport == "homa") {
#if WITH_HOMA_GRPC
    return HomaClient::createInsecureChannel(socket_address.data());
#else
    LOG(ERROR) << "Homa transport not compiled in";
    LOG(ERROR) << "You must build with bazel build --//:with-homa-grpc";
    return absl::UnimplementedError("Homa transport not compiled in");
#endif
  } else if (transport == "tcp") {
    std::shared_ptr<grpc::ChannelCredentials> creds = MakeChannelCredentials();
    return grpc::CreateCustomChannel(AddGrpcProtocol(socket_address), creds,
                                     DistbenchCustomChannelArguments());
  } else {
    LOG(ERROR) << "protocol_driver_grpc: unknown transport: " << transport;
    return absl::UnimplementedError(
        absl::StrCat("unknown transport: ", transport));
  }
}

absl::StatusOr<std::shared_ptr<grpc::ServerCredentials>> CreateServerCreds(
    std::string_view transport) {
  if (transport == "homa") {
#if WITH_HOMA_GRPC
    return HomaListener::insecureCredentials();
#else
    LOG(ERROR) << "Homa transport not compiled in";
    LOG(ERROR) << "You must build with bazel build --//:with-homa-grpc";
    return absl::UnimplementedError("Homa transport not compiled in");
#endif
  } else if (transport == "tcp") {
    return MakeServerCredentials();
  } else {
    LOG(ERROR) << "protocol_driver_grpc: unknown transport: " << transport;
    return absl::UnimplementedError(
        absl::StrCat("unknown transport: ", transport));
  }
}

}  // anonymous namespace

GrpcCommonClientDriver::GrpcCommonClientDriver() {}

GrpcCommonClientDriver::~GrpcCommonClientDriver() {}

// Client =====================================================================
GrpcPollingClientDriver::GrpcPollingClientDriver() {}
GrpcPollingClientDriver::~GrpcPollingClientDriver() { ShutdownClient(); }

absl::Status GrpcPollingClientDriver::Initialize(
    const ProtocolDriverOptions& pd_opts) {
  cq_poller_ = std::thread(&GrpcPollingClientDriver::RpcCompletionThread, this);
  transport_ =
      GetNamedServerSettingString(pd_opts, "transport", kDefaultTransport);
  return absl::OkStatus();
}

void GrpcCommonClientDriver::SetNumPeers(int num_peers) {
  grpc_client_stubs_.resize(num_peers);
  peer_connection_info_.resize(num_peers);
}

absl::Status GrpcCommonClientDriver::HandleConnect(
    std::string remote_connection_info, int peer) {
  CHECK_GE(peer, 0);
  CHECK_LT(static_cast<size_t>(peer), grpc_client_stubs_.size());
  peer_connection_info_[peer] = remote_connection_info;
  ServerAddress addr;
  addr.ParseFromString(remote_connection_info);
  auto maybe_channel = CreateClientChannel(addr.socket_address(), transport_);
  if (!maybe_channel.ok()) {
    return maybe_channel.status();
  }
  grpc_client_stubs_[peer] = Traffic::NewStub(maybe_channel.value());
  return absl::OkStatus();
}

std::vector<TransportStat> GrpcPollingClientDriver::GetTransportStats() {
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

absl::Status GrpcCommonClientDriver::SetupMultiServerChannel(
    const ::google::protobuf::RepeatedPtrField<NamedSetting>& settings,
    const std::vector<int>& peer_ids, int channel_id) {
  CHECK_GE(channel_id, 0);
  CHECK_LT(static_cast<size_t>(channel_id), multiserver_stubs_.size());
  if (transport_ != "tcp") {
    return absl::UnimplementedError("MultiServerChannel only works for tcp");
  }
  std::string addresses = "";
  for (const auto& peer_id : peer_ids) {
    CHECK_LT(static_cast<size_t>(peer_id), peer_connection_info_.size());
    ServerAddress addr;
    addr.ParseFromString(peer_connection_info_[peer_id]);
    if (!addresses.empty()) {
      addresses += ",";
    }
    addresses += addr.socket_address();
  }
  auto channel_args = DistbenchCustomChannelArguments();
  channel_args.SetLoadBalancingPolicyName(
      GetNamedSettingString(settings, "policy", "round_robin"));
  std::shared_ptr<grpc::ChannelCredentials> creds = MakeChannelCredentials();
  std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(
      AddGrpcProtocol(addresses), creds, channel_args);
  multiserver_stubs_[channel_id] = Traffic::NewStub(channel);
  return absl::OkStatus();
}

void GrpcCommonClientDriver::SetNumMultiServerChannels(int num_channels) {
  multiserver_stubs_.resize(num_channels);
}

void GrpcPollingClientDriver::InitiateRpcToMultiServerChannel(
    int channel_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  CHECK_GE(channel_index, 0);
  CHECK_LT(static_cast<size_t>(channel_index), multiserver_stubs_.size());

  ++pending_rpcs_;
  PendingRpc* new_rpc = new PendingRpc;
  new_rpc->done_callback = done_callback;
  new_rpc->state = state;
  new_rpc->request = std::move(state->request);
  new_rpc->rpc = multiserver_stubs_[channel_index]->AsyncGenericRpc(
      &new_rpc->context, new_rpc->request, &cq_);
  new_rpc->rpc->Finish(&new_rpc->response, &new_rpc->status, new_rpc);
}

void GrpcCallbackClientDriver::InitiateRpcToMultiServerChannel(
    int channel_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  CHECK_GE(channel_index, 0);
  CHECK_LT(static_cast<size_t>(channel_index), multiserver_stubs_.size());

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
      new_rpc->state->response.set_error_message(
          new_rpc->status.error_message());
      LOG_EVERY_N(ERROR, 1000) << "RPC failed with status: " << status;
    }
    new_rpc->done_callback();

    // Free before allowing the shutdown of the client
    delete new_rpc;
    --pending_rpcs_;
  };

  multiserver_stubs_[channel_index]->experimental_async()->GenericRpc(
      &new_rpc->context, &new_rpc->request, &new_rpc->response, callback_fct);
}

void GrpcPollingClientDriver::InitiateRpc(
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

void GrpcPollingClientDriver::RpcCompletionThread() {
  while (!shutdown_.HasBeenNotified()) {
    bool ok;
    void* tag;
    tag = nullptr;
    ok = false;
    cq_.Next(&tag, &ok);
    if (ok) {
      PendingRpc* finished_rpc = static_cast<PendingRpc*>(tag);
      finished_rpc->state->success = finished_rpc->status.ok();
      if (finished_rpc->state->success) {
        finished_rpc->state->request = std::move(finished_rpc->request);
        finished_rpc->state->response = std::move(finished_rpc->response);
      } else {
        finished_rpc->state->response.set_error_message(
            finished_rpc->status.error_message());
        LOG_EVERY_N(ERROR, 1000)
            << "RPC failed with status: " << finished_rpc->status;
      }
      finished_rpc->done_callback();

      // Free before allowing the shutdown of the client
      delete finished_rpc;
      --pending_rpcs_;
    }
  }
}

void GrpcPollingClientDriver::ChurnConnection(int peer) {}

void GrpcPollingClientDriver::ShutdownClient() {
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
  ~TrafficService() override { handler_set_.TryToNotify(); }

  void SetHandler(
      std::function<std::function<void()>(ServerRpcState* state)> handler) {
    handler_ = handler;
    handler_set_.TryToNotify();
  }

  grpc::Status GenericRpc(grpc::ServerContext* context,
                          const GenericRequest* request,
                          GenericResponse* response) override {
    ServerRpcState rpc_state;
    rpc_state.have_dedicated_thread = true;
    rpc_state.request = request;
    rpc_state.SetSendResponseFunction(
        [&]() { *response = std::move(rpc_state.response); });
    handler_set_.WaitForNotification();
    if (handler_) {
      auto remaining_work = handler_(&rpc_state);
      if (remaining_work) {
        remaining_work();
      }
      return grpc::Status::OK;
    } else {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE, "No rpc handler set.");
    }
  }

 private:
  SafeNotification handler_set_;
  std::function<std::function<void()>(ServerRpcState* state)> handler_;
};

}  // anonymous namespace
GrpcInlineServerDriver::GrpcInlineServerDriver() {}
GrpcInlineServerDriver::~GrpcInlineServerDriver() {}

absl::Status GrpcInlineServerDriver::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  transport_ =
      GetNamedServerSettingString(pd_opts, "transport", kDefaultTransport);
  auto maybe_ip = IpAddressForDevice(netdev_name, pd_opts.ip_version());
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  traffic_service_ = std::make_unique<TrafficService>();
  grpc::ServerBuilder builder;
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  auto maybe_server_creds = CreateServerCreds(transport_);
  if (!maybe_server_creds.ok()) {
    return maybe_server_creds.status();
  }
  builder.AddListeningPort(server_socket_address_, maybe_server_creds.value(),
                           port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  ApplyServerSettingsToGrpcBuilder(&builder, pd_opts);
  builder.RegisterService(traffic_service_.get());
  server_ = builder.BuildAndStart();

  server_port_ = *port;
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  if (!server_) {
    return absl::UnknownError("Grpc Traffic service failed to start");
  }

  return absl::OkStatus();
}

void GrpcInlineServerDriver::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  static_cast<TrafficService*>(traffic_service_.get())->SetHandler(handler);
}

absl::StatusOr<std::string> GrpcCommonServerDriver::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_.ip());
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

void GrpcCommonServerDriver::HandleConnectFailure(
    std::string_view local_connection_info) {}

void GrpcInlineServerDriver::ShutdownServer() {
  if (server_) server_->Shutdown();
}

std::vector<TransportStat> GrpcInlineServerDriver::GetTransportStats() {
  return {};
}

// Client/Server ProtocolDriver ===============================================
ProtocolDriverGrpc::ProtocolDriverGrpc() {}

absl::Status ProtocolDriverGrpc::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  // Build the client
  bool is_async = pd_opts.name() == "grpc_async";
  std::string default_client_type = is_async ? "callback" : "polling";
  std::string client_type =
      GetNamedClientSettingString(pd_opts, "client_type", default_client_type);
  if (client_type == "polling") {
    client_ =
        std::unique_ptr<GrpcCommonClientDriver>(new GrpcPollingClientDriver());
  } else if (client_type == "callback") {
    client_ =
        std::unique_ptr<GrpcCommonClientDriver>(new GrpcCallbackClientDriver());
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid GRPC client_type (", client_type, ")"));
  }
  absl::Status ret = client_->Initialize(pd_opts);
  if (!ret.ok()) return ret;

  // Build the server
  std::string default_server_type = is_async ? "handoff" : "inline";
  std::string server_type =
      GetNamedServerSettingString(pd_opts, "server_type", default_server_type);
  if (server_type == "inline") {
    server_ =
        std::unique_ptr<GrpcCommonServerDriver>(new GrpcInlineServerDriver());
  } else if (server_type == "handoff") {
    server_ =
        std::unique_ptr<GrpcCommonServerDriver>(new GrpcHandoffServerDriver());
  } else if (server_type == "polling") {
    auto threadpool_size = GetNamedServerSettingInt64(
        pd_opts, "threadpool_size", absl::base_internal::NumCPUs());
    auto threadpool_type =
        GetNamedServerSettingString(pd_opts, "threadpool_type", "");
    auto tp = CreateThreadpool(threadpool_type, threadpool_size);
    if (!tp.ok()) {
      return tp.status();
    }
    server_ = std::unique_ptr<GrpcCommonServerDriver>(
        new GrpcPollingServerDriver(std::move(tp.value())));
  } else {
    return absl::InvalidArgumentError("Invalid GRPC server_type");
  }
  return server_->Initialize(pd_opts, port);
}

void ProtocolDriverGrpc::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  server_->SetHandler(handler);
}

void ProtocolDriverGrpc::SetNumPeers(int num_peers) {
  client_->SetNumPeers(num_peers);
}

void ProtocolDriverGrpc::SetNumMultiServerChannels(int num_channels) {
  client_->SetNumMultiServerChannels(num_channels);
}

absl::Status ProtocolDriverGrpc::SetupMultiServerChannel(
    const ::google::protobuf::RepeatedPtrField<NamedSetting>& settings,
    const std::vector<int>& peer_ids, int channel_id) {
  return client_->SetupMultiServerChannel(settings, peer_ids, channel_id);
}

ProtocolDriverGrpc::~ProtocolDriverGrpc() {
  ShutdownClient();
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
  std::vector<TransportStat> stats = client_->GetTransportStats();
  std::vector<TransportStat> stats_server = server_->GetTransportStats();
  std::move(stats_server.begin(), stats_server.end(),
            std::back_inserter(stats));
  return stats;
}

void ProtocolDriverGrpc::InitiateRpc(int peer_index, ClientRpcState* state,
                                     std::function<void(void)> done_callback) {
  client_->InitiateRpc(peer_index, state, done_callback);
}

void ProtocolDriverGrpc::InitiateRpcToMultiServerChannel(
    int channel_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  client_->InitiateRpcToMultiServerChannel(channel_index, state, done_callback);
}

void ProtocolDriverGrpc::ChurnConnection(int peer) {
  client_->ChurnConnection(peer);
}

void ProtocolDriverGrpc::ShutdownClient() {
  if (client_ != nullptr) {
    client_->ShutdownClient();
  }
}

void ProtocolDriverGrpc::ShutdownServer() {
  if (server_ != nullptr) {
    server_->ShutdownServer();
  }
}

// Client =====================================================================
GrpcCallbackClientDriver::GrpcCallbackClientDriver() {}

GrpcCallbackClientDriver::~GrpcCallbackClientDriver() { ShutdownClient(); }

absl::Status GrpcCallbackClientDriver::Initialize(
    const ProtocolDriverOptions& pd_opts) {
  transport_ =
      GetNamedServerSettingString(pd_opts, "transport", kDefaultTransport);
  return absl::OkStatus();
}

std::vector<TransportStat> GrpcCallbackClientDriver::GetTransportStats() {
  return {};
}

void GrpcCallbackClientDriver::InitiateRpc(
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
      new_rpc->state->response.set_error_message(
          new_rpc->status.error_message());
      LOG_EVERY_N(ERROR, 1000) << "RPC failed with status: " << status;
    }
    new_rpc->done_callback();

    // Free before allowing the shutdown of the client
    delete new_rpc;
    --pending_rpcs_;
  };

  grpc_client_stubs_[peer_index]->experimental_async()->GenericRpc(
      &new_rpc->context, &new_rpc->request, &new_rpc->response, callback_fct);
}

void GrpcCallbackClientDriver::ChurnConnection(int peer) {}

void GrpcCallbackClientDriver::ShutdownClient() {
  while (pending_rpcs_) {
  }
  grpc_client_stubs_.clear();
}

// Server =====================================================================
namespace {
class TrafficServiceAsyncCallback
    : public Traffic::ExperimentalCallbackService {
 public:
  TrafficServiceAsyncCallback(std::unique_ptr<AbstractThreadpool> tp)
      : thread_pool_(std::move(tp)) {}
  ~TrafficServiceAsyncCallback() override { handler_set_.TryToNotify(); }

  void SetHandler(
      std::function<std::function<void()>(ServerRpcState* state)> handler) {
    handler_ = handler;
    handler_set_.TryToNotify();
  }

  grpc::ServerUnaryReactor* GenericRpc(grpc::CallbackServerContext* context,
                                       const GenericRequest* request,
                                       GenericResponse* response) override {
    auto* reactor = context->DefaultReactor();
    ServerRpcState* rpc_state = new ServerRpcState;
    rpc_state->request = request;
    rpc_state->SetSendResponseFunction([=]() {
      *response = std::move(rpc_state->response);
      reactor->Finish(grpc::Status::OK);
    });
    rpc_state->SetFreeStateFunction([=]() { delete rpc_state; });
    handler_set_.WaitForNotification();
    if (handler_) {
      auto remaining_work = handler_(rpc_state);
      if (remaining_work) {
        thread_pool_->AddTask(remaining_work);
      }
    }
    return reactor;
  }

 private:
  SafeNotification handler_set_;
  std::function<std::function<void()>(ServerRpcState* state)> handler_;
  std::unique_ptr<AbstractThreadpool> thread_pool_;
};
}  // anonymous namespace

GrpcHandoffServerDriver::GrpcHandoffServerDriver() {}
GrpcHandoffServerDriver::~GrpcHandoffServerDriver() {}

absl::Status GrpcHandoffServerDriver::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  transport_ =
      GetNamedServerSettingString(pd_opts, "transport", kDefaultTransport);
  auto maybe_ip = IpAddressForDevice(netdev_name, pd_opts.ip_version());
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  auto threadpool_size = GetNamedServerSettingInt64(
      pd_opts, "threadpool_size", absl::base_internal::NumCPUs());
  auto threadpool_type =
      GetNamedServerSettingString(pd_opts, "threadpool_type", "");

  auto tp = CreateThreadpool(threadpool_type, threadpool_size);
  if (!tp.ok()) {
    return tp.status();
  }
  traffic_service_ =
      std::make_unique<TrafficServiceAsyncCallback>(std::move(tp.value()));
  grpc::ServerBuilder builder;
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  auto maybe_server_creds = CreateServerCreds(transport_);
  if (!maybe_server_creds.ok()) {
    return maybe_server_creds.status();
  }
  builder.AddListeningPort(server_socket_address_, maybe_server_creds.value(),
                           port);
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

  return absl::OkStatus();
}

void GrpcHandoffServerDriver::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  static_cast<TrafficServiceAsyncCallback*>(traffic_service_.get())
      ->SetHandler(handler);
}

void GrpcHandoffServerDriver::ShutdownServer() {
  if (server_ != nullptr) {
    server_->Shutdown();
  }
}

std::vector<TransportStat> GrpcHandoffServerDriver::GetTransportStats() {
  return {};
}

namespace {
class PollingRpcHandlerFsm {
 public:
  PollingRpcHandlerFsm(
      Traffic::AsyncService* service, grpc::ServerCompletionQueue* cq,
      std::function<std::function<void()>(ServerRpcState* state)>* handler,
      AbstractThreadpool* thread_pool)
      : service_(service),
        cq_(cq),
        handler_(handler),
        responder_(&ctx_),
        thread_pool_(thread_pool),
        state_(AWAITING_REQUEST) {
    CHECK(thread_pool_);
    RpcHandlerFsm();
  }

  void IncRef() {
    std::atomic_fetch_add_explicit(&refcnt_, 1, std::memory_order_relaxed);
  }

  // DecRefAndMaybeDelete() is called from two places,
  // one of which will delete the PollingRpcHandlerFsm.
  void DecRefAndMaybeDelete() {
    if (std::atomic_fetch_sub_explicit(&refcnt_, 1,
                                       std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  void HandleRpc() {
    rpc_state_.have_dedicated_thread = false;
    rpc_state_.request = &request_;
    rpc_state_.SetSendResponseFunction([&]() {
      response_ = std::move(rpc_state_.response);
      responder_.Finish(response_, grpc::Status::OK, this);
    });
    IncRef();
    rpc_state_.SetFreeStateFunction([this]() { DecRefAndMaybeDelete(); });
    if (*handler_) {
      auto remaining_work = (*handler_)(&rpc_state_);
      if (remaining_work) {
        thread_pool_->AddTask(remaining_work);
      }
    }
  }

  void RpcHandlerFsm(bool post_new_handler = false) {
    CallState next_state = state_;
    if (state_ == AWAITING_REQUEST) {
      next_state = PROCESSING_REQUEST;
      service_->RequestGenericRpc(&ctx_, &request_, &responder_, cq_, cq_,
                                  this);
    } else if (state_ == PROCESSING_REQUEST) {
      next_state = FINISHED_SENDING_RESPONSE;
      if (post_new_handler) {
        new PollingRpcHandlerFsm(service_, cq_, handler_, thread_pool_);
      }
      HandleRpc();
    } else if (state_ == FINISHED_SENDING_RESPONSE) {
      DecRefAndMaybeDelete();
      return;
    } else {
      LOG(FATAL) << "Unknown state: " << state_;
    }
    state_ = next_state;
  }

 private:
  enum CallState {
    AWAITING_REQUEST,
    PROCESSING_REQUEST,
    FINISHED_SENDING_RESPONSE
  };

  GenericRequest request_;
  GenericResponse response_;
  Traffic::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;
  std::function<std::function<void()>(ServerRpcState* state)>* handler_;
  grpc::ServerAsyncResponseWriter<GenericResponse> responder_;
  grpc::ServerContext ctx_;
  AbstractThreadpool* thread_pool_;
  CallState state_;
  ServerRpcState rpc_state_;
  std::atomic<int> refcnt_ = 1;
};
}  // anonymous namespace

// Server =====================================================================
GrpcPollingServerDriver::GrpcPollingServerDriver(
    std::unique_ptr<AbstractThreadpool> tp)
    : thread_pool_(std::move(tp)) {}

GrpcPollingServerDriver::~GrpcPollingServerDriver() { ShutdownServer(); }

absl::Status GrpcPollingServerDriver::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  transport_ =
      GetNamedServerSettingString(pd_opts, "transport", kDefaultTransport);
  auto maybe_ip = IpAddressForDevice(netdev_name, pd_opts.ip_version());
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  traffic_async_service_ = std::make_unique<Traffic::AsyncService>();
  grpc::ServerBuilder builder;
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  auto maybe_server_creds = CreateServerCreds(transport_);
  if (!maybe_server_creds.ok()) {
    return maybe_server_creds.status();
  }
  builder.AddListeningPort(server_socket_address_, maybe_server_creds.value(),
                           port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  ApplyServerSettingsToGrpcBuilder(&builder, pd_opts);
  builder.RegisterService(traffic_async_service_.get());
  server_cq_ = builder.AddCompletionQueue();
  server_ = builder.BuildAndStart();

  server_port_ = *port;
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  if (!server_) {
    return absl::UnknownError("Grpc Traffic service failed to start");
  }

  // Proceed to the server's main loop.
  handle_rpcs_ = RunRegisteredThread("RpcHandler", [this]() { HandleRpcs(); });
  handle_rpcs_started_.WaitForNotification();
  return absl::OkStatus();
}

void GrpcPollingServerDriver::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  handler_ = handler;
  handler_set_.TryToNotify();
}

void GrpcPollingServerDriver::ShutdownServer() {
  handler_set_.TryToNotify();
  if (server_) {
    server_->Shutdown();
    server_shutdown_detected_.WaitForNotification();
  }
  if (server_cq_) {
    server_cq_->Shutdown();
  }
  if (handle_rpcs_.joinable()) {
    handle_rpcs_.join();
  }
}

std::vector<TransportStat> GrpcPollingServerDriver::GetTransportStats() {
  return {};
}

void GrpcPollingServerDriver::HandleRpcs() {
  new PollingRpcHandlerFsm(traffic_async_service_.get(), server_cq_.get(),
                           &handler_, thread_pool_.get());
  // Make sure the completion queue is nonempty before allowing Initialize
  // to return:
  handle_rpcs_started_.Notify();
  void* tag;
  bool ok;
  bool post_new_handler = true;
  handler_set_.WaitForNotification();
  while (server_cq_->Next(&tag, &ok)) {
    PollingRpcHandlerFsm* rpc_fsm = static_cast<PollingRpcHandlerFsm*>(tag);
    if (!ok) {
      server_shutdown_detected_.TryToNotify();
      post_new_handler = false;
      rpc_fsm->DecRefAndMaybeDelete();
      continue;
    }
    rpc_fsm->RpcHandlerFsm(post_new_handler);
  }
}
}  // namespace distbench
