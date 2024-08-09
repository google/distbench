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

#include "distbench_node_manager.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_split.h"
#include "distbench_netutils.h"
#include "distbench_thread_support.h"
#include "protocol_driver_allocator.h"

namespace distbench {

absl::StatusOr<ProtocolDriverOptions> NodeManager::ResolveProtocolDriverAlias(
    const std::string& protocol_name) ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
  for (const auto& pd_opts_enum : traffic_config_.protocol_driver_options()) {
    if (pd_opts_enum.name() == protocol_name ||
        (protocol_name.empty() &&
         pd_opts_enum.name() == "default_protocol_driver_options")) {
      return pd_opts_enum;
    }
  }
  return absl::NotFoundError(absl::StrCat(
      "Could not resolve protocol driver alias for ", protocol_name, "."));
}

NodeManager::NodeManager() {
  auto resolver = [this](std::string protocol_name)
                      ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
                        return ResolveProtocolDriverAlias(protocol_name);
                      };
  SetProtocolDriverAliasResolver(resolver);
}

grpc::Status NodeManager::ConfigureNode(grpc::ServerContext* context,
                                        const NodeServiceConfig* request,
                                        ServiceEndpointMap* response) {
  absl::MutexLock m(&mutex_);
  traffic_config_ = request->traffic_config();
  if (traffic_config_.overload_limits().max_threads() <= 0) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "overload_limits.max_threads must be positive.");
  }
  SetOverloadAbortThreshhold(traffic_config_.overload_limits().max_threads());
  ClearServices();
  auto& service_map = *response->mutable_service_endpoints();
  for (const auto& service_name : request->services()) {
    std::vector<std::string> service_instance =
        absl::StrSplit(service_name, '/');
    CHECK_GE(service_instance.size(), 2lu);

    int port = 0;
    const ServiceOpts service_options = {
        .service_name = service_name,
        .service_type = service_instance[0],
        .service_index = GetGridIndexFromName(service_name),
        .port = &port,
        .protocol = traffic_config_.default_protocol(),
        .netdev_name = opts_.default_data_plane_device};
    absl::Status ret = AllocService(service_options);
    if (!ret.ok()) {
      std::string err = absl::StrCat("AllocService failure: ", ret.ToString());
      LOG(ERROR) << err;
      return grpc::Status(grpc::StatusCode::UNKNOWN, err);
    }
    auto maybe_address =
        SocketAddressForDevice(opts_.control_plane_device, port);
    if (!maybe_address.ok()) {
      return grpc::Status(grpc::StatusCode::UNKNOWN,
                          absl::StrCat("SocketAddressForDevice failure: ",
                                       maybe_address.status().ToString()));
    }
    auto& service_entry = service_map[service_name];
    service_entry.set_endpoint_address(maybe_address.value());
    service_entry.set_hostname(Hostname());
    *service_entry.mutable_attributes() = registration_info_.attributes();
    int dimensions = std::count(service_name.begin(), service_name.end(), '/');
    Attribute* attribute;
    if (dimensions == 1) {
      attribute = service_entry.add_attributes();
      attribute->set_name("index");
      attribute->set_value(service_instance[1]);
    }
    if (dimensions > 1) {
      attribute = service_entry.add_attributes();
      attribute->set_name("x_index");
      attribute->set_value(absl::StrCat(service_options.service_index.x));
      attribute = service_entry.add_attributes();
      attribute->set_name("y_index");
      attribute->set_value(absl::StrCat(service_options.service_index.y));
    }
    if (dimensions > 2) {
      attribute = service_entry.add_attributes();
      attribute->set_name("z_index");
      attribute->set_value(absl::StrCat(service_options.service_index.z));
    }
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
    pd_opts.set_netdev_name(std::string(service_opts.netdev_name));

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
  absl::Status ret = engine->Initialize(
      traffic_config_, opts_.control_plane_device, service_opts.service_type,
      service_opts.service_index, service_opts.port);
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
      return grpc::Status(
          grpc::StatusCode::UNKNOWN,
          absl::StrCat("ConfigurePeers failure: ", ret.ToString()));
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
    if (log.peer_logs().empty() && log.activity_logs().empty() &&
        log.replay_trace_logs().empty())
      continue;
    instance_logs[service_engine.first] = std::move(log);
  }

  if (request->clear_services()) {
    for (const auto& service_engine : service_engines_) {
      service_engine.second->CancelTraffic(absl::OkStatus());
    }
    ClearServices();
  }
  (*response->mutable_node_usages())[NodeAlias()] =
      GetRUsageStatsFromStructs(rusage_start_test_, DoGetRusage());

  return grpc::Status::OK;
}

void NodeManager::CancelTraffic(absl::Status status) {
  LOG(INFO) << "Starting CancelTraffic on " << NodeAlias();
  absl::ReaderMutexLock m(&mutex_);
  for (const auto& service_engine : service_engines_) {
    service_engine.second->CancelTraffic(status);
  }
  LOG(INFO) << "Finished CancelTraffic on " << NodeAlias();
}

grpc::Status NodeManager::CancelTraffic(grpc::ServerContext* context,
                                        const CancelTrafficRequest* request,
                                        CancelTrafficResult* response) {
  CancelTraffic(absl::CancelledError("cancelled by test_sequencer"));
  return grpc::Status::OK;
}

grpc::Status NodeManager::ShutdownNode(grpc::ServerContext* context,
                                       const ShutdownNodeRequest* request,
                                       ShutdownNodeResult* response) {
  if (shutdown_requested_.TryToNotify()) {
    LOG(INFO) << "Shutting down node " << NodeAlias();
  }
  return grpc::Status::OK;
}

void NodeManager::Shutdown() {
  shutdown_requested_.TryToNotify();
  if (grpc_server_) {
    grpc_server_->Shutdown();
  }
}

void NodeManager::Wait() {
  shutdown_requested_.WaitForNotification();
  if (grpc_server_) {
    grpc_server_->Shutdown();
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
    Shutdown();
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

  std::string listening_address =
      GetBindAddressFromPort(opts_.control_plane_device, *opts_.port);
  grpc::ServerBuilder builder;
  builder.SetMaxReceiveMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> server_creds =
      MakeServerCredentials();
  builder.AddListeningPort(listening_address, server_creds, opts_.port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(this);
  grpc_server_ = builder.BuildAndStart();
  // Update listening_address with the obtained port:
  listening_address =
      GetBindAddressFromPort(opts_.control_plane_device, *opts_.port);
  if (!absl::StartsWith(listening_address, "[::]") &&
      !absl::StartsWith(listening_address, "0.0.0.0")) {
    std::string listening_ip =
        GetBestAddress(opts_.control_plane_device).value().ToString();
    registration_info_.set_control_ip(listening_ip);
  }
  if (opts_.service_address.empty()) {
    if (!registration_info_.has_control_ip()) {
      opts_.service_address =
          absl::StrCat("dns:///", Hostname(), ":", *opts_.port);
    } else if (listening_address[0] == '[') {
      opts_.service_address = absl::StrCat("ipv6:///", listening_address);
    } else {
      opts_.service_address = absl::StrCat("ipv4:///", listening_address);
    }
  }
  registration_info_.set_service_address(opts_.service_address);
  if (!grpc_server_) {
    Shutdown();
    return absl::UnknownError("NodeManager service failed to start");
  }
  LOG(INFO) << "NodeManager server listening on " << opts_.service_address
            << " on " << Hostname();

  registration_info_.set_preassigned_node_id(opts_.preassigned_node_id);
  registration_info_.set_hostname(Hostname());
  registration_info_.set_control_port(*opts_.port);
  for (const auto& attr : opts_.attributes) {
    auto new_attr = registration_info_.add_attributes();
    new_attr->set_name(attr.name());
    new_attr->set_value(attr.value());
  }
  grpc::ClientContext context;
  SetGrpcClientContextDeadline(&context, /*max_time_s=*/60);
  context.set_wait_for_ready(true);
  absl::MutexLock m(&config_mutex_);
  LOG(INFO) << "Registering to test sequencer at "
            << opts_.test_sequencer_service_address;
  LOG(INFO) << ProtoToString(registration_info_);
  grpc::Status status =
      test_sequencer_stub->RegisterNode(&context, registration_info_, &config_);
  if (!status.ok()) {
    status = Annotate(
        status, absl::StrCat("While registering node to test sequencer(",
                             opts_.test_sequencer_service_address, "): "));
    grpc_server_->Shutdown();
    Shutdown();
    return absl::UnknownError(status.error_message());
  }

  LOG(INFO) << "NodeConfig: " << ProtoToShortString(config_);
  return absl::OkStatus();
}

}  // namespace distbench
