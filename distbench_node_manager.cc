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

#include "distbench_node_manager.h"

#include "distbench_utils.h"
#include "protocol_driver_allocator.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"

namespace distbench {

NodeManager::NodeManager(SimpleClock* clock) {
  clock_ = clock;
}

grpc::Status NodeManager::ConfigureNode(
    grpc::ServerContext* context,
    const NodeServiceConfig* request,
    ServiceEndpointMap* response) {
  LOG(INFO) << request->DebugString();
  absl::MutexLock m (&mutex_);
  traffic_config_ = request->traffic_config();
  ClearServices();
  auto& service_map = *response->mutable_service_endpoints();
  for (const auto& service_name : request->services()) {
    std::vector<std::string> service_instance =
      absl::StrSplit(service_name, '/');
    CHECK_EQ(service_instance.size(), 2lu);

    int instance;
    CHECK(absl::SimpleAtoi(service_instance[1], &instance));

    int port = 0;
    absl::Status ret = AllocService({
          service_name,
          service_instance[0],
          instance,
          &port,
          traffic_config_.default_protocol(),
          opts_.default_data_plane_device});
    if (!ret.ok()) {
      return grpc::Status(grpc::StatusCode::UNKNOWN,
          absl::StrCat("AllocService failure: ", ret.ToString()));
    }
    auto& service_entry = service_map[service_name];
    service_entry.set_endpoint_address(
        SocketAddressForDevice(opts_.default_data_plane_device, port));
    service_entry.set_hostname(Hostname());
  }
  return grpc::Status::OK;
}

void NodeManager::ClearServices() {
  service_engines_.clear();
}

absl::StatusOr<ProtocolDriverOptions> NodeManager::GetProtocolDriverOptionsFor(
    const ServiceOpts& service_opts) {
  ProtocolDriverOptions pd_opts;
  std::string pd_options_name = "";

  // ProtocolDriverOptions specified in Service ?
  for (const auto& service: traffic_config_.services() ) {
    if (service.name() == service_opts.service_type) {
      if (service.has_protocol_driver_options_name()) {
        pd_options_name = service.protocol_driver_options_name();
        if (pd_options_name.empty()) {
          return absl::InvalidArgumentError(
              "An empty name cannot be specified");
        }
      }
    }
  }

  // ProtocolDriverOptions found in config ?
  for (const auto& pd_opts_enum: traffic_config_.protocol_driver_options() ) {
    if (pd_opts_enum.name() == pd_options_name) {
      pd_opts = pd_opts_enum;
    }
  }

  // Use defaults to complete options
  if (pd_options_name.empty())
    pd_opts.set_name("generated_default");
  if (!pd_opts.has_protocol_name())
    pd_opts.set_protocol_name(std::string(service_opts.protocol));
  if (!pd_opts.has_netdev_name())
    pd_opts.set_netdev_name(std::string(service_opts.netdev));

  return pd_opts;
}

absl::Status NodeManager::AllocService(const ServiceOpts& service_opts) {
  CHECK(service_opts.port);
  absl::StatusOr<ProtocolDriverOptions> pd_opts =
      GetProtocolDriverOptionsFor(service_opts);
  if (!pd_opts.ok()) return pd_opts.status();
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(*pd_opts);
  int port = 0;
  absl::Status ret = pd->Initialize(*pd_opts, &port);
  if (!ret.ok()) return ret;
  auto engine = std::make_unique<DistBenchEngine>(std::move(pd), clock_);
  ret = engine->Initialize(
        traffic_config_, service_opts.service_type,
        service_opts.service_instance, service_opts.port);
  if (!ret.ok()) return ret;

  service_engines_[std::string(service_opts.service_name)] = std::move(engine);
  return absl::OkStatus();
}

grpc::Status NodeManager::IntroducePeers(grpc::ServerContext* context,
                                        const ServiceEndpointMap* request,
                                        IntroducePeersResult* response) {
  absl::MutexLock m (&mutex_);
  peers_ = *request;
  for (const auto& service_engine : service_engines_) {
    auto ret = service_engine.second->ConfigurePeers(peers_);
    if (!ret.ok())
      return grpc::Status(grpc::StatusCode::UNKNOWN, "ConfigurePeers failure");
  }
  return grpc::Status::OK;
}

grpc::Status NodeManager::RunTraffic(grpc::ServerContext* context,
                                     const RunTrafficRequest* request,
                                     ServiceLogs* response) {
  absl::ReaderMutexLock m (&mutex_);
  for (const auto& service_engine : service_engines_) {
    auto ret = service_engine.second->RunTraffic(request);
    if (!ret.ok())
      return grpc::Status(grpc::StatusCode::UNKNOWN, "RunTraffic failure");
  }
  for (const auto& service_engine : service_engines_) {
    auto log = service_engine.second->FinishTrafficAndGetLogs();
    if (!log.peer_logs().empty()) {
      (*response->mutable_instance_logs())[service_engine.first] =
        std::move(log);
    }
  }
  return grpc::Status::OK;
}

grpc::Status NodeManager::CancelTraffic(grpc::ServerContext* context,
                                        const CancelTrafficRequest* request,
                                        CancelTrafficResult* response) {
  LOG(INFO) << "Starting CancelTraffic";
  absl::ReaderMutexLock m (&mutex_);
  for (const auto& service_engine : service_engines_) {
    service_engine.second->CancelTraffic();
  }
  LOG(INFO) << "Finished CancelTraffic";
  return grpc::Status::OK;
}

grpc::Status NodeManager::ShutdownNode(grpc::ServerContext* context,
                                       const ShutdownNodeRequest* request,
                                       ShutdownNodeResult* response) {
  LOG(INFO) << "Shutting down...";
  shutdown_requested_.Notify();
  return grpc::Status::OK;
}

void NodeManager::Shutdown() {
  if (!shutdown_requested_.HasBeenNotified()) {
    shutdown_requested_.Notify();
  }
  if (grpc_server_) {
    grpc_server_->Shutdown();
  }
}

void NodeManager::Wait() {
  shutdown_requested_.WaitForNotification();
  if (grpc_server_) {
    grpc_server_->Wait();
  }
}

NodeManager::~NodeManager() {
  Shutdown();
  ClearServices();
  if (grpc_server_) {
    grpc_server_->Shutdown();
    grpc_server_->Wait();
  }
}

absl::Status NodeManager::Initialize(const NodeManagerOpts& opts) {
  opts_ = opts;
  if (opts_.test_sequencer_service_address.empty()) {
    return absl::InvalidArgumentError(
        "node_manager requires the --test_sequencer flag.");
  }
  std::shared_ptr<grpc::ChannelCredentials> client_creds =
    MakeChannelCredentials();
  std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(
      opts_.test_sequencer_service_address, client_creds,
      DistbenchCustomChannelArguments());
  std::unique_ptr<DistBenchTestSequencer::Stub> test_sequencer_stub =
    DistBenchTestSequencer::NewStub(channel);

  service_address_ = absl::StrCat("[::]:", *opts_.port);
  grpc::ServerBuilder builder;
  builder.SetMaxReceiveMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> server_creds =
    MakeServerCredentials();
  builder.AddListeningPort(service_address_, server_creds, opts_.port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(this);
  grpc_server_ = builder.BuildAndStart();
  service_address_ = absl::StrCat("[::]:", *opts_.port);  // port may have changed
  if (!grpc_server_) {
    return absl::UnknownError("NodeManager service failed to start");
  }
  LOG(INFO) << "Server listening on " << service_address_;

  NodeRegistration reg;
  reg.set_hostname(Hostname());
  reg.set_control_ip(IpAddressForDevice(""));
  reg.set_control_port(*opts_.port);

  grpc::ClientContext context;
  std::chrono::system_clock::time_point deadline =
    std::chrono::system_clock::now() + std::chrono::seconds(60);
  context.set_deadline(deadline);
  context.set_wait_for_ready(true);
  grpc::Status status =
    test_sequencer_stub->RegisterNode(&context, reg, &config_);
  if (!status.ok()) {
    status = Annotate(status, absl::StrCat(
          "While registering node to test sequencer(",
          opts_.test_sequencer_service_address,
          "): "));
    grpc_server_->Shutdown();
    return absl::InvalidArgumentError(status.error_message());
  }

  LOG(INFO) << "NodeConfig: " << config_.ShortDebugString();

  return absl::OkStatus();
}

}  // namespace distbench
