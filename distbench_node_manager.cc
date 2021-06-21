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

    int port = AllocatePort();
    service_ports_.push_back(port);
    int instance;
    CHECK(absl::SimpleAtoi(service_instance[1], &instance));

    absl::Status ret = AllocService({
          service_name,
          service_instance[0],
          instance,
          port,
          traffic_config_.default_protocol(),
          "eth0"});
    if (!ret.ok())
      grpc::Status(grpc::StatusCode::UNKNOWN,
          absl::StrCat("AllocService failure: ", ret.ToString()));
    auto& service_entry = service_map[service_name];
    service_entry.set_endpoint_address(SocketAddressForDevice("", port));
    service_entry.set_hostname(Hostname());
  }
  return grpc::Status::OK;
}

void NodeManager::ClearServices() {
  service_engines_.clear();
  for (const auto& port : service_ports_) {
    FreePort(port);
  }
  service_ports_.clear();
}

absl::Status NodeManager::AllocService(
    const ServiceOpts& service_opts) {
  CHECK(service_opts.port);
  ProtocolDriverOptions pd_opts;
  pd_opts.set_protocol_name(std::string(service_opts.protocol));
  pd_opts.set_netdev_name(std::string(service_opts.netdev));
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(pd_opts);
  absl::Status ret = pd->Initialize("eth0", AllocatePort());
  if (!ret.ok()) return ret;
  auto engine = std::make_unique<DistBenchEngine>(std::move(pd), clock_);
  ret = engine->Initialize(
        service_opts.port, traffic_config_, service_opts.service_type,
        service_opts.service_instance);
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
  LOG(INFO) << "saw the cancelation now";
  absl::ReaderMutexLock m (&mutex_);
  for (const auto& service_engine : service_engines_) {
    service_engine.second->CancelTraffic();
  }
  LOG(INFO) << "finished all the cancelations now";
  return grpc::Status::OK;
}

void NodeManager::Shutdown() {
  if (grpc_server_) {
    grpc_server_->Shutdown();
  }
}

void NodeManager::Wait() {
  if (grpc_server_) {
    grpc_server_->Wait();
  }
}

NodeManager::~NodeManager() {
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
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      opts_.test_sequencer_service_address, client_creds);
  std::unique_ptr<DistBenchTestSequencer::Stub> test_sequencer_stub =
    DistBenchTestSequencer::NewStub(channel);

  service_address_ = absl::StrCat("[::]:", opts_.port);
  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> server_creds =
    MakeServerCredentials();
  builder.AddListeningPort(service_address_, server_creds);
  builder.RegisterService(this);
  grpc_server_ = builder.BuildAndStart();
  if (grpc_server_) {
    LOG(INFO) << "Server listening on " << service_address_;
  }
  if (!grpc_server_) {
    return absl::UnknownError("NodeManager service failed to start");
  }
  NodeRegistration reg;
  reg.set_hostname(Hostname());
  reg.set_control_port(opts_.port);
  NodeConfig config;
  grpc::ClientContext context;
  std::chrono::system_clock::time_point deadline =
    std::chrono::system_clock::now() + std::chrono::seconds(60);
  context.set_deadline(deadline);
  context.set_wait_for_ready(true);
  grpc::Status status =
    test_sequencer_stub->RegisterNode(&context, reg, &config);
  if (status.ok()) {
    LOG(INFO) << "NodeConfig: " << config.ShortDebugString();
  } else {
    status = Annotate(status, absl::StrCat(
          "While registering node to test sequencer(",
          opts_.test_sequencer_service_address,
          "): "));
    grpc_server_->Shutdown();
  }
  if (status.ok())
    return absl::OkStatus();

  return absl::InvalidArgumentError(status.error_message());
}

}  // namespace distbench
