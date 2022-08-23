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

#include "absl/strings/str_split.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "protocol_driver_allocator.h"

namespace distbench {

absl::StatusOr<ProtocolDriverOptions> NodeManager::ResolveProtocolDriverAlias(
    const std::string& protocol_name) {
  for (const auto& pd_opts_enum : traffic_config_.protocol_driver_options()) {
    if (pd_opts_enum.name() == protocol_name) {
      return pd_opts_enum;
    }
  }
  return absl::NotFoundError(absl::StrCat(
      "Could not resolve protocol driver alias for ", protocol_name, "."));
}

NodeManager::NodeManager() {
  auto func =
      [=](std::string protocol_name) -> absl::StatusOr<ProtocolDriverOptions> {
    return ResolveProtocolDriverAlias(protocol_name);
  };
  SetProtocolDriverAliasResolver(func);
}

grpc::Status NodeManager::ConfigureNode(grpc::ServerContext* context,
                                        const NodeServiceConfig* request,
                                        ServiceEndpointMap* response) {
  absl::MutexLock m(&mutex_);
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
    const ServiceOpts service_options = {
        .service_name = service_name,
        .service_type = service_instance[0],
        .service_instance = instance,
        .port = &port,
        .protocol = traffic_config_.default_protocol(),
        .netdev = opts_.default_data_plane_device};
    absl::Status ret = AllocService(service_options);
    if (!ret.ok()) {
      return grpc::Status(
          grpc::StatusCode::UNKNOWN,
          absl::StrCat("AllocService failure: ", ret.ToString()));
    }
    auto maybe_address =
        SocketAddressForDevice(opts_.default_data_plane_device, port);
    if (!maybe_address.ok()) {
      return grpc::Status(grpc::StatusCode::UNKNOWN,
                          absl::StrCat("SocketAddressForDevice failure: ",
                                       maybe_address.status().ToString()));
    }
    auto& service_entry = service_map[service_name];
    service_entry.set_endpoint_address(maybe_address.value());
    service_entry.set_hostname(Hostname());
  }
  return grpc::Status::OK;
}

void NodeManager::ClearServices() { service_engines_.clear(); }

absl::StatusOr<ProtocolDriverOptions> NodeManager::GetProtocolDriverOptionsFor(
    const ServiceOpts& service_opts) {
  ProtocolDriverOptions pd_opts;
  std::string pd_options_name = "";

  // ProtocolDriverOptions specified in Service ?
  for (const auto& service : traffic_config_.services()) {
    if (service.name() != service_opts.service_type) continue;
    if (!service.has_protocol_driver_options_name()) continue;

    pd_options_name = service.protocol_driver_options_name();
    if (pd_options_name.empty()) {
      return absl::InvalidArgumentError("An empty name cannot be specified");
    }
  }

  auto maybe_pdo = ResolveProtocolDriverAlias(pd_options_name);
  if (maybe_pdo.ok()) pd_opts = maybe_pdo.value();

  // Use defaults to complete options
  if (pd_options_name.empty()) pd_opts.set_name("generated_default");
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

  int port = 0;
  auto maybe_pd = AllocateProtocolDriver(*pd_opts, &port);
  if (!maybe_pd.ok()) return maybe_pd.status();

  auto engine = std::make_unique<DistBenchEngine>(std::move(maybe_pd.value()));
  absl::Status ret =
      engine->Initialize(traffic_config_, service_opts.service_type,
                         service_opts.service_instance, service_opts.port);
  if (!ret.ok()) return ret;

  service_engines_[std::string(service_opts.service_name)] = std::move(engine);
  return absl::OkStatus();
}

grpc::Status NodeManager::IntroducePeers(grpc::ServerContext* context,
                                         const ServiceEndpointMap* request,
                                         IntroducePeersResult* response) {
  absl::MutexLock m(&mutex_);
  peers_ = *request;
  for (const auto& service_engine : service_engines_) {
    auto ret = service_engine.second->ConfigurePeers(peers_);
    if (!ret.ok()) {
      return grpc::Status(grpc::StatusCode::UNKNOWN, "ConfigurePeers failure");
    }
  }
  return grpc::Status::OK;
}

grpc::Status NodeManager::RunTraffic(grpc::ServerContext* context,
                                     const RunTrafficRequest* request,
                                     RunTrafficResponse* response) {
  absl::ReaderMutexLock m(&mutex_);

  rusage_start_test_ = DoGetRusage();

  for (const auto& service_engine : service_engines_) {
    auto ret = service_engine.second->RunTraffic(request);
    if (!ret.ok()) {
      return grpc::Status(grpc::StatusCode::UNKNOWN, "RunTraffic failure");
    }
  }

  for (const auto& service_engine : service_engines_)
    service_engine.second->FinishTraffic();

  return grpc::Status::OK;
}

grpc::Status NodeManager::GetTrafficResult(
    grpc::ServerContext* context, const GetTrafficResultRequest* request,
    GetTrafficResultResponse* response) {
  absl::MutexLock m(&mutex_);

  auto& service_logs = *response->mutable_service_logs();
  auto& instance_logs = *service_logs.mutable_instance_logs();
  for (const auto& service_engine : service_engines_) {
    auto log = service_engine.second->GetLogs();
    if (log.peer_logs().empty()) continue;
    instance_logs[service_engine.first] = std::move(log);
  }

  if (request->clear_services()) {
    for (const auto& service_engine : service_engines_) {
      service_engine.second->CancelTraffic();
    }
    ClearServices();
  }
  (*response->mutable_node_usages())[config_.node_alias()] =
      GetRUsageStatsFromStructs(rusage_start_test_, DoGetRusage());

  return grpc::Status::OK;
}

grpc::Status NodeManager::CancelTraffic(grpc::ServerContext* context,
                                        const CancelTrafficRequest* request,
                                        CancelTrafficResult* response) {
  LOG(INFO) << "Starting CancelTraffic on " << config_.node_alias();
  absl::ReaderMutexLock m(&mutex_);
  for (const auto& service_engine : service_engines_) {
    service_engine.second->CancelTraffic();
  }
  LOG(INFO) << "Finished CancelTraffic on " << config_.node_alias();
  return grpc::Status::OK;
}

grpc::Status NodeManager::ShutdownNode(grpc::ServerContext* context,
                                       const ShutdownNodeRequest* request,
                                       ShutdownNodeResult* response) {
  LOG(INFO) << "Shutting down node " << config_.node_alias();
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
  SetProtocolDriverAliasResolver(
      std::function<absl::StatusOr<ProtocolDriverOptions>(
          const std::string&)>());
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
  // Update the service_address_ with the obtained port
  service_address_ = absl::StrCat("[::]:", *opts_.port);
  if (!grpc_server_) {
    return absl::UnknownError("NodeManager service failed to start");
  }
  LOG(INFO) << "NodeManager server listening on " << service_address_ << " on "
            << Hostname();

  NodeRegistration reg;
  reg.set_hostname(Hostname());
  auto maybe_ip = IpAddressForDevice("");
  if (!maybe_ip.ok()) return maybe_ip.status();
  reg.set_control_ip(maybe_ip.value().ip());
  reg.set_control_port(*opts_.port);

  grpc::ClientContext context;
  SetGrpcClientContextDeadline(&context, /*max_time_s=*/60);
  context.set_wait_for_ready(true);
  grpc::Status status =
      test_sequencer_stub->RegisterNode(&context, reg, &config_);
  if (!status.ok()) {
    status = Annotate(
        status, absl::StrCat("While registering node to test sequencer(",
                             opts_.test_sequencer_service_address, "): "));
    grpc_server_->Shutdown();
    return absl::InvalidArgumentError(status.error_message());
  }

  LOG(INFO) << "NodeConfig: " << config_.ShortDebugString();
  return absl::OkStatus();
}

}  // namespace distbench
