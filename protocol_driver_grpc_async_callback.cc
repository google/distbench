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

#include "absl/base/internal/sysinfo.h"
#include "distbench_threadpool.h"
#include "distbench_utils.h"
#include "glog/logging.h"

namespace distbench {

// Client =====================================================================
GrpcCallbackClientDriver::GrpcCallbackClientDriver() {
}

GrpcCallbackClientDriver::
    ~GrpcCallbackClientDriver() {
  ShutdownClient();
}

absl::Status GrpcCallbackClientDriver::InitializeClient(
    const ProtocolDriverOptions& pd_opts) {
  return absl::OkStatus();
}

void GrpcCallbackClientDriver::SetNumPeers(int num_peers) {
  grpc_client_stubs_.resize(num_peers);
}

absl::Status GrpcCallbackClientDriver::HandleConnect(
    std::string remote_connection_info, int peer) {
  CHECK_GE(peer, 0);
  CHECK_LT(static_cast<size_t>(peer), grpc_client_stubs_.size());
  ServerAddress addr;
  addr.ParseFromString(remote_connection_info);
  std::shared_ptr<grpc::ChannelCredentials> creds = MakeChannelCredentials();
  std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(
      addr.socket_address(), creds, DistbenchCustomChannelArguments());
  grpc_client_stubs_[peer] = Traffic::NewStub(channel);
  return absl::OkStatus();
}

std::vector<TransportStat>
GrpcCallbackClientDriver::GetTransportStats() {
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
  TrafficServiceAsyncCallback()
      // Create a thread pool, reserving 50% of the cpus to handle responses.
      : thread_pool_((absl::base_internal::NumCPUs() + 1) / 2) {}
  ~TrafficServiceAsyncCallback() override {}

  void SetHandler(
      std::function<std::function<void()>(ServerRpcState* state)> handler) {
    handler_ = handler;
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
    auto fct_action_list_thread = handler_(rpc_state);
    if (fct_action_list_thread) {
      thread_pool_.AddWork(fct_action_list_thread);
    }
    return reactor;
  }

 private:
  std::function<std::function<void()>(ServerRpcState* state)> handler_;
  distbench::DistbenchThreadpool thread_pool_;
};
}  // anonymous namespace

GrpcHandoffServerDriver::GrpcHandoffServerDriver() {
}
GrpcHandoffServerDriver::
    ~GrpcHandoffServerDriver() {}

absl::Status GrpcHandoffServerDriver::InitializeServer(
    const ProtocolDriverOptions& pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  auto maybe_ip = IpAddressForDevice(netdev_name);
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  traffic_service_ = absl::make_unique<TrafficServiceAsyncCallback>();
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

void GrpcHandoffServerDriver::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  static_cast<TrafficServiceAsyncCallback*>(traffic_service_.get())
      ->SetHandler(handler);
}

absl::StatusOr<std::string>
GrpcHandoffServerDriver::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_.ip());
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

void GrpcHandoffServerDriver::HandleConnectFailure(
    std::string_view local_connection_info) {}

void GrpcHandoffServerDriver::ShutdownServer() {
  if (server_ != nullptr) {
    server_->Shutdown();
  }
}

std::vector<TransportStat>
GrpcHandoffServerDriver::GetTransportStats() {
  return {};
}

namespace {
class PollingRpcHandlerFsm {
 public:
  PollingRpcHandlerFsm(
      Traffic::AsyncService* service, grpc::ServerCompletionQueue* cq,
      std::function<std::function<void()>(ServerRpcState* state)>* handler,
      DistbenchThreadpool* thread_pool)
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
    if (std::atomic_fetch_sub_explicit(
          &refcnt_, 1, std::memory_order_relaxed) == 1) {
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
    rpc_state_.SetFreeStateFunction([=]() { DecRefAndMaybeDelete(); });
    if (*handler_) {
      auto f = (*handler_)(&rpc_state_);
      if (f) {
        thread_pool_->AddWork(f);
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
  DistbenchThreadpool* thread_pool_;
  CallState state_;
  ServerRpcState rpc_state_;
  std::atomic<int> refcnt_ = 1;
};
}  // anonymous namespace

// Server =====================================================================
namespace {
class TrafficAsyncServiceCq : public Traffic::AsyncService {
 public:
  ~TrafficAsyncServiceCq() override {}

  void SetHandler(
      std::function<std::function<void()>(ServerRpcState* state)> handler) {
    handler_ = handler;
  }

 private:
  std::function<std::function<void()>(ServerRpcState* state)> handler_;
};

}  // anonymous namespace
GrpcPollingServerDriver::GrpcPollingServerDriver()
    : thread_pool_((absl::base_internal::NumCPUs() + 1) / 2) {}

GrpcPollingServerDriver::~GrpcPollingServerDriver() {}

absl::Status GrpcPollingServerDriver::InitializeServer(
    const ProtocolDriverOptions& pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  auto maybe_ip = IpAddressForDevice(netdev_name);
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);
  traffic_async_service_ = absl::make_unique<TrafficAsyncServiceCq>();
  grpc::ServerBuilder builder;
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> server_creds =
      MakeServerCredentials();
  builder.AddListeningPort(server_socket_address_, server_creds, port);
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

  LOG(INFO) << "Grpc Traffic server listening on " << server_socket_address_;
  // Proceed to the server's main loop.
  handle_rpcs_ = absl::make_unique<std::thread>(
      &GrpcPollingServerDriver::HandleRpcs, this);
  handle_rpcs_started_.WaitForNotification();
  return absl::OkStatus();
}

void GrpcPollingServerDriver::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  static_cast<TrafficAsyncServiceCq*>(traffic_async_service_.get())
      ->SetHandler(handler);
  handler_ = handler;
}

absl::StatusOr<std::string> GrpcPollingServerDriver::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_.ip());
  addr.set_port(server_port_);
  addr.set_socket_address(server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

void GrpcPollingServerDriver::HandleConnectFailure(
    std::string_view local_connection_info) {}

void GrpcPollingServerDriver::ShutdownServer() {
  server_->Shutdown();
  server_shutdown_detected_.WaitForNotification();
  server_cq_->Shutdown();
  if (handle_rpcs_->joinable()) {
    handle_rpcs_->join();
  }
}

std::vector<TransportStat>
GrpcPollingServerDriver::GetTransportStats() {
  return {};
}

void GrpcPollingServerDriver::HandleRpcs() {
  new PollingRpcHandlerFsm(
      traffic_async_service_.get(), server_cq_.get(), &handler_,
      &thread_pool_);
  // Make sure the completion queue is nonempty before allowing Initialize
  // to return:
  handle_rpcs_started_.Notify();
  void* tag;
  bool ok;
  bool post_new_handler = true;
  while (server_cq_->Next(&tag, &ok)) {
    PollingRpcHandlerFsm* rpc_fsm = static_cast<PollingRpcHandlerFsm*>(tag);
    if (!ok) {
      server_shutdown_detected_.Notify();
      post_new_handler = false;
      rpc_fsm->DecRefAndMaybeDelete();
      continue;
    }
    rpc_fsm->RpcHandlerFsm(post_new_handler);
  }
}

}  // namespace distbench
