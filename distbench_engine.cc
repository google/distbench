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

#include "distbench_engine.h"

#include "absl/base/internal/sysinfo.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "distbench_netutils.h"
#include "distbench_thread_support.h"
#include "glog/logging.h"

namespace distbench {

namespace {

// These define the canonical order of the fields in a multidimensional
// distribution:
const char* canonical_1d_fields[] = {"payload_size", nullptr};

const char* canonical_2d_fields[] = {"request_payload_size",
                                     "response_payload_size", nullptr};

enum kFieldNames {
  kRequestPayloadSizeField = 0,
  kResponsePayloadSizeField = 1,
};

}  // namespace

ThreadSafeDictionary::ThreadSafeDictionary() {
  absl::MutexLock m(&mutex_);
  contents_.reserve(100);
  contents_.push_back("");  // Index 0 is empty string
  contents_map_[""] = 0;
}

int ThreadSafeDictionary::GetIndex(std::string_view text) {
  absl::MutexLock m(&mutex_);
  auto it = contents_map_.find(text);
  if (it != contents_map_.end()) {
    return it->second;
  }
  contents_.push_back(std::string(text));
  contents_map_[text] = contents_.size() - 1;
  return contents_.size() - 1;
}

std::vector<std::string> ThreadSafeDictionary::GetContents() {
  absl::MutexLock m(&mutex_);
  return contents_;
}

std::string_view ThreadSafeDictionary::GetValue(int index) const {
  absl::MutexLock m(&mutex_);
  return contents_[index];
}

grpc::Status DistBenchEngine::SetupConnection(grpc::ServerContext* context,
                                              const ConnectRequest* request,
                                              ConnectResponse* response) {
  auto maybe_info = pd_->HandlePreConnect(request->initiator_info(),
                                          /*peer=*/0);
  if (!maybe_info.ok()) {
    return abslStatusToGrpcStatus(maybe_info.status());
  }
  response->set_responder_info(maybe_info.value());
  return grpc::Status::OK;
}

DistBenchEngine::DistBenchEngine(std::unique_ptr<ProtocolDriver> pd)
    : pd_(std::move(pd)) {
  clock_ = &pd_->GetClock();
}

DistBenchEngine::~DistBenchEngine() {
  FinishTraffic();
  if (server_) {
    server_->Shutdown();
    server_->Wait();
  }
  if (pd_) {
    pd_->ShutdownServer();
    while (detached_actionlist_threads_) {
      sched_yield();
    }
    pd_->ShutdownClient();
  }
}

// Initialize the payload map and perform basic validation
absl::Status DistBenchEngine::InitializePayloadsMap() {
  for (int i = 0; i < traffic_config_.payload_descriptions_size(); ++i) {
    const auto& payload_spec = traffic_config_.payload_descriptions(i);
    const auto& payload_spec_name = payload_spec.name();

    // Check for double declaration
    if (payload_map_.find(payload_spec_name) != payload_map_.end()) {
      return absl::InvalidArgumentError(
          "Double definition of payload_descriptions: " + payload_spec_name);
    }

    payload_map_[payload_spec_name] = payload_spec;
  }

  return absl::OkStatus();
}

int DistBenchEngine::get_payload_size(const std::string& payload_name) {
  const auto& payload = payload_map_[payload_name];
  int size = -1;  // Not found value

  if (payload.has_size()) {
    size = payload.size();
  } else {
    LOG(WARNING) << "No size defined for payload " << payload_name << "\n";
  }

  return size;
}

absl::Status DistBenchEngine::InitializeRpcFanoutFilter(
    RpcDefinition& rpc_def) {
  const auto& rpc_spec = rpc_def.rpc_spec;
  std::string fanout_filter = rpc_spec.fanout_filter();
  std::map<std::string, FanoutFilter> fanout_map = {
    {"", kAll},
    {"all", kAll},
    {"random", kRandomSingle},
    {"random_single", kRandomSingle},
    {"round_robin", kRoundRobin},
    {"same_x", kSameX},
    {"same_y", kSameY},
    {"same_z", kSameZ},
    {"same_xy", kSameXY},
    {"same_xz", kSameXZ},
    {"same_yz", kSameYZ},
    {"same_xyz", kSameXYZ},
    {"ring_x", kRingX},
    {"ring_y", kRingY},
    {"ring_z", kRingZ},
    {"alternating_ring_x", kAlternatingRingX},
    {"alternating_ring_y", kAlternatingRingY},
    {"alternating_ring_z", kAlternatingRingZ},
  };

  auto it = fanout_map.find(fanout_filter);
  if (it != fanout_map.end()) {
    rpc_def.fanout_filter = it->second;
    return absl::OkStatus();
  }

  size_t prefix_size = fanout_filter.find_first_of("{");
  if (prefix_size == std::string::npos) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown fanout_filter: ", fanout_filter));
  }

  std::string fanout_filter_params = fanout_filter.substr(prefix_size);
  fanout_filter = fanout_filter.substr(0, prefix_size);

  std::map<std::string, FanoutFilter> fanout_map2 = {
    {"stochastic", kStochastic},
    {"linear_x", kLinearX},
    {"linear_y", kLinearY},
    {"linear_z", kLinearZ},
  };

  it = fanout_map2.find(fanout_filter);
  if (it == fanout_map2.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown fanout_filter: ", fanout_filter));
  }

  rpc_def.fanout_filter = it->second;

  switch (rpc_def.fanout_filter) {
    case kStochastic:
      break;

    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unknown fanout_filter: ", fanout_filter));
      break;

    case kLinearX:
    case kLinearY:
    case kLinearZ:
      if (sscanf(fanout_filter_params.data(), "{%d}", &rpc_def.fanout_filter_distance)) {
        return absl::OkStatus();
      } else {
        return absl::InvalidArgumentError(
            absl::StrCat("Could not parse parameter: ", fanout_filter_params));
      }
      break;
  }

  if (!absl::StartsWith(fanout_filter_params, "{")) {
    return absl::InvalidArgumentError(
        "Invalid stochastic filter; should starts with stochastic{");
  }
  fanout_filter_params.erase(0, 1);  // Consume the '{'

  if (!absl::EndsWith(fanout_filter_params, "}")) {
    return absl::InvalidArgumentError(
        "Invalid stochastic filter; should ends with }");
  }
  fanout_filter_params.pop_back();  // Consume the '}'

  float total_probability = 0.;
  for (auto s : absl::StrSplit(fanout_filter_params, ',')) {
    std::vector<std::string> v = absl::StrSplit(s, ':');
    if (v.size() != 2) {
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; only 1 : accepted");
    }

    StochasticDist dist;
    if (!absl::SimpleAtof(v[0], &dist.probability)) {
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; unable to decode probability");
    }
    if (dist.probability < 0 || dist.probability > 1) {
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; probability should be between 0. and 1.");
    }
    total_probability += dist.probability;

    if (!absl::SimpleAtoi(v[1], &dist.nb_targets)) {
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; unable to decode nb_targets");
    }
    if (dist.nb_targets < 0) {
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; nb_targets should be >= 0");
    }

    rpc_def.stochastic_dist.push_back(dist);
  }

  if (total_probability != 1.0) {
    LOG(WARNING) << "The probability for the stochastic fanout of "
                 << rpc_def.rpc_spec.name() << " does not add up to 1.0 (is "
                 << total_probability << ")";
  }

  if (rpc_def.stochastic_dist.empty()) {
    return absl::InvalidArgumentError(
        "Invalid stochastic filter; need at least a value pair");
  }

  return absl::OkStatus();
}

absl::Status DistBenchEngine::InitializeActivityConfigMap() {
  for (int i = 0; i < traffic_config_.activity_configs_size(); ++i) {
    ActivityConfig activity_config = traffic_config_.activity_configs(i);
    const auto& activity_config_name = activity_config.name();
    if (activity_config_indices_map_.find(activity_config_name) ==
        activity_config_indices_map_.end()) {
      auto maybe_config = ParseActivityConfig(activity_config);
      if (!maybe_config.ok()) return maybe_config.status();
      activity_config_indices_map_[maybe_config.value().activity_config_name] =
          stored_activity_config_.size();
      stored_activity_config_.push_back(maybe_config.value());
    } else {
      return absl::FailedPreconditionError(
          absl::StrCat("Activity config '", activity_config_name,
                       "' was defined more than once."));
    }
  }
  return absl::OkStatus();
}

absl::Status DistBenchEngine::InitializeRpcDefinitionsMap() {
  for (int i = 0; i < traffic_config_.rpc_descriptions_size(); ++i) {
    const auto& rpc_spec = traffic_config_.rpc_descriptions(i);
    const auto& rpc_name = rpc_spec.name();

    RpcDefinition rpc_def;
    rpc_def.rpc_spec = rpc_spec;
    auto it = service_index_map_.find(rpc_def.rpc_spec.server());
    if (it == service_index_map_.end()) {
      return absl::NotFoundError(rpc_def.rpc_spec.server());
    }
    rpc_def.server_service_spec = traffic_config_.services(it->second);

    // Get request payload size
    rpc_def.request_payload_size = -1;
    if (rpc_spec.has_request_payload_name()) {
      const auto& payload_name = rpc_spec.request_payload_name();
      rpc_def.request_payload_size = get_payload_size(payload_name);
    }
    if (rpc_def.request_payload_size == -1) {
      rpc_def.request_payload_size = 16;
      LOG(WARNING) << "No request payload defined for " << rpc_name
                   << "; using a default of " << rpc_def.request_payload_size;
    }

    // Get response payload size
    rpc_def.response_payload_size = -1;
    if (rpc_spec.has_response_payload_name()) {
      const auto& payload_name = rpc_spec.response_payload_name();
      rpc_def.response_payload_size = get_payload_size(payload_name);
    }
    if (rpc_def.response_payload_size == -1) {
      rpc_def.response_payload_size = 32;
      LOG(WARNING) << "No response payload defined for " << rpc_name
                   << "; using a default of " << rpc_def.response_payload_size;
    }

    if (rpc_spec.has_distribution_config_name()) {
      rpc_def.sample_generator_index =
          GetSampleGeneratorIndex(rpc_spec.distribution_config_name());
    }

    auto ret = InitializeRpcFanoutFilter(rpc_def);
    if (!ret.ok()) return ret;

    rpc_map_[rpc_name] = rpc_def;
  }

  return absl::OkStatus();
}

absl::Status DistBenchEngine::InitializeTables() {
  service_index_map_ = EnumerateServiceTypes(traffic_config_);
  absl::Status ret_init_payload = InitializePayloadsMap();
  if (!ret_init_payload.ok()) return ret_init_payload;

  absl::Status ret_init_distribution_config =
      AllocateAndInitializeSampleGenerators();
  if (!ret_init_distribution_config.ok()) return ret_init_distribution_config;

  absl::Status ret_init_rpc_def = InitializeRpcDefinitionsMap();
  if (!ret_init_rpc_def.ok()) return ret_init_rpc_def;

  absl::Status ret_init_activity_config = InitializeActivityConfigMap();
  if (!ret_init_activity_config.ok()) return ret_init_activity_config;

  // Convert the action table to a map indexed by name:
  std::map<std::string, Action> action_map;
  for (int i = 0; i < traffic_config_.actions_size(); ++i) {
    const auto& action = traffic_config_.actions(i);
    action_map[action.name()] = traffic_config_.actions(i);
  }
  std::map<std::string, int> rpc_name_index_map =
      EnumerateRpcs(traffic_config_);

  std::map<std::string, int> action_list_index_map;
  action_lists_.resize(traffic_config_.action_lists().size());
  for (int i = 0; i < traffic_config_.action_lists_size(); ++i) {
    const auto& action_list = traffic_config_.action_lists(i);
    action_list_index_map[action_list.name()] = i;
    action_lists_[i].proto = action_list;
    action_lists_[i].list_actions.resize(action_list.action_names_size());
  }

  for (int i = 0; i < traffic_config_.action_lists_size(); ++i) {
    const auto& action_list = traffic_config_.action_lists(i);
    std::map<std::string, int> list_action_indices;
    for (int j = 0; j < action_list.action_names().size(); ++j) {
      const auto& action_name = action_list.action_names(j);
      list_action_indices[action_name] = j;
      auto it = action_map.find(action_name);
      if (it == action_map.end()) {
        return absl::NotFoundError(action_name);
      }
      if (it->second.has_rpc_name()) {
        action_lists_[i].has_rpcs = true;
        // Validate rpc can be sent from this local node
      }
      action_lists_[i].list_actions[j].proto = it->second;
    }
    // second pass to fixup deps:
    for (size_t j = 0; j < action_lists_[i].list_actions.size(); ++j) {
      auto& action = action_lists_[i].list_actions[j];
      if (action.proto.has_rpc_name()) {
        auto it2 = rpc_name_index_map.find(action.proto.rpc_name());
        if (it2 == rpc_name_index_map.end()) {
          return absl::NotFoundError(action.proto.rpc_name());
        }
        action.rpc_index = it2->second;
        std::string target_service_name =
            traffic_config_.rpc_descriptions(action.rpc_index).server();
        auto it3 = service_index_map_.find(target_service_name);
        if (it3 == service_index_map_.end()) {
          return absl::NotFoundError(target_service_name);
        }
        action.rpc_service_index = it3->second;
      } else if (action.proto.has_action_list_name()) {
        auto it4 = action_list_index_map.find(action.proto.action_list_name());
        if (it4 == action_list_index_map.end()) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Action_list not found: ", action.proto.action_list_name()));
        }
        action.actionlist_index = it4->second;
      } else if (action.proto.has_activity_config_name()) {
        auto it5 = activity_config_indices_map_.find(
            action.proto.activity_config_name());
        if (it5 == activity_config_indices_map_.end()) {
          return absl::InvalidArgumentError(
              absl::StrCat("Activity config not found for: ",
                           action.proto.activity_config_name()));
        }
        action.activity_config_index = it5->second;
      } else {
        return absl::InvalidArgumentError(
            "only rpc actions & activities are supported for now");
      }
      action.dependent_action_indices.resize(action.proto.dependencies_size());
      for (int k = 0; k < action.proto.dependencies_size(); ++k) {
        auto it = list_action_indices.find(action.proto.dependencies(k));
        if (it == list_action_indices.end()) {
          return absl::NotFoundError(action.proto.dependencies(k));
        }
        action.dependent_action_indices[k] = it->second;
        if (static_cast<size_t>(it->second) >= j) {
          return absl::InvalidArgumentError(
              "dependencies must refer to prior actions");
        }
      }
    }
  }

  client_rpc_table_ = std::make_unique<SimulatedClientRpc[]>(
      traffic_config_.rpc_descriptions().size());
  server_rpc_table_.resize(traffic_config_.rpc_descriptions().size());
  std::map<std::string, int> client_rpc_index_map;
  std::set<std::string> server_rpc_set;

  for (int i = 0; i < traffic_config_.rpc_descriptions_size(); ++i) {
    const auto& rpc = traffic_config_.rpc_descriptions(i);
    const std::string server_service_name = rpc.server();
    const std::string client_service_name = rpc.client();
    if (client_service_name.empty()) {
      LOG(INFO) << engine_name_ << ": " << rpc.ShortDebugString();
      return absl::InvalidArgumentError(
          absl::StrCat("Rpc ", rpc.name(), " must have a client_service_name"));
    }

    if (server_service_name.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Rpc ", rpc.name(), " must have a server_service_name"));
    }
    if (client_service_name == service_name_) {
      client_rpc_index_map[rpc.name()] = i;
      dependent_services_.insert(server_service_name);
    }
    if (server_service_name == service_name_) {
      server_rpc_set.insert(rpc.name());
    }

    auto it = action_list_index_map.find(rpc.name());
    if (it == action_list_index_map.end()) {
      return absl::NotFoundError(rpc.name());
    }

    int action_list_index = it->second;
    server_rpc_table_[i].handler_action_list_index = action_list_index;

    // Optimize by setting handler to -1 if the action list is empty
    if (action_lists_[action_list_index].proto.action_names().empty())
      server_rpc_table_[i].handler_action_list_index = action_list_index = -1;

    server_rpc_table_[i].rpc_definition = rpc_map_[rpc.name()];

    auto it1 = service_index_map_.find(server_service_name);
    if (it1 == service_index_map_.end()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Rpc ", rpc.name(), " specifies unknown server service_type ",
          server_service_name));
    }
    auto it2 = service_index_map_.find(client_service_name);
    if (it2 == service_index_map_.end()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Rpc ", rpc.name(), " specifies unknown client service_type ",
          client_service_name));
    }
    client_rpc_table_[i].service_index = it1->second;
    client_rpc_table_[i].rpc_definition = rpc_map_[rpc.name()];
    client_rpc_table_[i].pending_requests_per_peer.resize(
        traffic_config_.services(it1->second).count(), 0);
  }

  return absl::OkStatus();
}

absl::Status DistBenchEngine::Initialize(
    const DistributedSystemDescription& global_description,
    std::string_view control_plane_device, std::string_view service_name,
    InstanceRanks service_instance_ranks, int* port) {
  traffic_config_ = global_description;
  CHECK(!service_name.empty());
  service_name_ = service_name;
  ranks_ = service_instance_ranks;
  auto maybe_service_spec = GetServiceSpec(service_name, global_description);
  if (!maybe_service_spec.ok()) return maybe_service_spec.status();
  service_spec_ = maybe_service_spec.value();
  service_instance_ = GetLinearServiceInstanceFromRanks(service_spec_, service_instance_ranks);

  engine_name_ = GetServiceInstanceName(service_spec_, service_instance_);
  auto maybe_threadpool =
      CreateThreadpool("elastic", absl::base_internal::NumCPUs());
  if (!maybe_threadpool.ok()) {
    return maybe_threadpool.status();
  }
  thread_pool_ = std::move(maybe_threadpool.value());

  absl::Status ret = InitializeTables();
  if (!ret.ok()) return ret;
  service_index_ = service_index_map_[service_name_];
  actionlist_invocation_counts = std::make_unique<std::atomic<int>[]>(
      global_description.action_lists_size());

  // Start server
  std::string server_address =
      GetBindAddressFromPort(control_plane_device, *port);
  grpc::ServerBuilder builder;
  builder.SetMaxReceiveMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> server_creds =
      MakeServerCredentials();
  builder.AddListeningPort(server_address, server_creds, port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  // *port may have changed if a new port was assigned.
  server_address = GetBindAddressFromPort(control_plane_device, *port);
  if (!server_) {
    LOG(ERROR) << engine_name_ << ": Engine start failed on " << server_address;
    return absl::UnknownError("Engine service failed to start");
  }
  LOG(INFO) << engine_name_ << ": Engine server listening on "
            << server_address;

  actionlist_error_dictionary_ = std::make_shared<ThreadSafeDictionary>();
  return absl::OkStatus();
}

absl::Status DistBenchEngine::ConfigurePeers(const ServiceEndpointMap& peers) {
  pd_->SetHandler([this](ServerRpcState* state) { return RpcHandler(state); });
  service_map_ = peers;
  if (service_map_.service_endpoints_size() < 1) {
    return absl::NotFoundError("No peers configured.");
  }

  return ConnectToPeers();
}

absl::Status DistBenchEngine::ConnectToPeers() {
  std::map<std::string, int> service_sizes =
      EnumerateServiceSizes(traffic_config_);
  std::map<std::string, int> service_instance_ids =
      EnumerateServiceInstanceIds(traffic_config_);

  // peers_[service_id][instance_id]
  peers_.resize(traffic_config_.services_size());
  for (int i = 0; i < traffic_config_.services_size(); ++i) {
    peers_[i].resize(traffic_config_.services(i).count());
  }

  int num_targets = 0;
  std::string my_name = GetServiceInstanceName(traffic_config_.services(service_index_map_[service_name_]), service_instance_);
  for (const auto& service : service_map_.service_endpoints()) {
    auto it = service_instance_ids.find(service.first);
    CHECK(it != service_instance_ids.end()) << my_name;
    int peer_trace_id = it->second;
    std::vector<std::string> service_and_instance =
        absl::StrSplit(service.first, '/');
    CHECK_EQ(service_and_instance.size(), 2ul);
    auto& service_type = service_and_instance[0];
    int instance = GetLinearServiceInstanceFromRanks(
        traffic_config_.services(service_index_map_[service_type]),
        GetServiceInstanceRanksFromName(service.first));
    if (service.first == my_name) {
      trace_id_ = peer_trace_id;
    }
    auto it2 = service_index_map_.find(service_type);
    CHECK(it2 != service_index_map_.end());
    int service_id = it2->second;
    peers_[service_id][instance].log_name = service.first;
    peers_[service_id][instance].trace_id = peer_trace_id;

    if (dependent_services_.count(service_type)) {
      peers_[service_id][instance].endpoint_address =
          service.second.endpoint_address();
      peers_[service_id][instance].pd_id = num_targets;
      ++num_targets;
    }
  }

  pd_->SetNumPeers(num_targets);
  grpc::CompletionQueue cq;
  struct PendingRpc {
    std::unique_ptr<ConnectionSetup::Stub> stub;
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ConnectResponse>> rpc;
    grpc::Status status;
    ConnectRequest request;
    ConnectResponse response;
    std::string server_address;
  };
  grpc::Status status;
  std::vector<PendingRpc> pending_rpcs(num_targets);
  int rpc_count = 0;
  for (const auto& service_type : peers_) {
    for (const auto& service_instance : service_type) {
      if (!service_instance.endpoint_address.empty()) {
        auto& rpc_state = pending_rpcs[service_instance.pd_id];
        std::shared_ptr<grpc::ChannelCredentials> creds =
            MakeChannelCredentials();
        std::shared_ptr<grpc::Channel> channel =
            grpc::CreateCustomChannel(service_instance.endpoint_address, creds,
                                      DistbenchCustomChannelArguments());
        rpc_state.stub = ConnectionSetup::NewStub(channel);
        rpc_state.server_address = service_instance.endpoint_address;
        CHECK(rpc_state.stub);

        ++rpc_count;
        rpc_state.request.set_initiator_info(pd_->Preconnect().value());
        SetGrpcClientContextDeadline(&rpc_state.context, /*max_time_s=*/60);
        rpc_state.rpc = rpc_state.stub->AsyncSetupConnection(
            &rpc_state.context, rpc_state.request, &cq);
        rpc_state.rpc->Finish(&rpc_state.response, &rpc_state.status,
                              &rpc_state);
      }
    }
  }
  CHECK(rpc_count == num_targets);
  while (rpc_count) {
    bool ok;
    void* tag;
    cq.Next(&tag, &ok);
    if (ok) {
      --rpc_count;
      PendingRpc* finished_rpc = static_cast<PendingRpc*>(tag);
      if (!finished_rpc->status.ok()) {
        pd_->HandleConnectFailure(finished_rpc->request.initiator_info());
        status = finished_rpc->status;
        LOG(ERROR) << engine_name_ << ": ConnectToPeers error:"
                   << finished_rpc->status.error_code() << " "
                   << finished_rpc->status.error_message() << " connecting to "
                   << finished_rpc->server_address;
      }
    }
  }
  for (size_t i = 0; i < pending_rpcs.size(); ++i) {
    if (pending_rpcs[i].status.ok()) {
      absl::Status final_status = pd_->HandleConnect(
          pending_rpcs[i].response.responder_info(), /*peer=*/i);
      if (!final_status.ok()) {
        LOG(INFO) << engine_name_
                  << ": Weird, a connect failed after rpc succeeded.";
        status = abslStatusToGrpcStatus(final_status);
      }
    }
  }
  return grpcStatusToAbslStatus(status);
}

absl::Status DistBenchEngine::RunTraffic(const RunTrafficRequest* request) {
  if (service_map_.service_endpoints_size() < 1) {
    return absl::NotFoundError("No peers configured.");
  }
  for (int i = 0; i < traffic_config_.action_lists_size(); ++i) {
    if (service_name_ == traffic_config_.action_lists(i).name()) {
      LOG(INFO) << engine_name_ << ": Running";
      const GenericRequest* fake_request = new GenericRequest;
      ServerRpcState* top_level_state = new ServerRpcState{};
      top_level_state->request = fake_request;
      top_level_state->have_dedicated_thread = true;
      top_level_state->SetFreeStateFunction([=]() {
        delete fake_request;
        delete top_level_state;
      });
      engine_main_thread_ = RunRegisteredThread(
          "EngineMain",
          [this, i, top_level_state]() { RunActionList(i, top_level_state); });
      break;
    }
  }

  return absl::OkStatus();
}

void DistBenchEngine::CancelTraffic(absl::Status status,
                                    absl::Duration grace_period) {
  absl::MutexLock m(&cancelation_mutex_);
  if (canceled_.TryToNotify()) {
    LOG(INFO) << engine_name_ << ": CancelTraffic " << status;
    cancelation_reason_ = status.ToString();
    cancelation_time_ = clock_->Now() + grace_period;
  }
}

void DistBenchEngine::FinishTraffic() {
  if (engine_main_thread_.joinable()) {
    engine_main_thread_.join();
    LOG(INFO) << engine_name_ << ": Finished running Main";
  }
}

void DistBenchEngine::AddActivityLogs(ServicePerformanceLog* sp_log) {
  for (auto& alog : cumulative_activity_logs_) {
    auto& activity_log = (*sp_log->mutable_activity_logs())[alog.first];
    for (auto& metric : alog.second) {
      auto* am = activity_log.add_activity_metrics();
      am->set_name(metric.first);
      am->set_value_int(metric.second);
    }
  }
}

ServicePerformanceLog DistBenchEngine::GetLogs() {
  ServicePerformanceLog log;
  if (!cancelation_reason_.empty()) {
    log.set_engine_error_message(cancelation_reason_);
  }
  for (size_t i = 0; i < peers_.size(); ++i) {
    for (size_t j = 0; j < peers_[i].size(); ++j) {
      absl::MutexLock m(&peers_[i][j].mutex);
      for (auto& partial_log : peers_[i][j].partial_logs) {
        for (auto& map_pair : partial_log.rpc_logs()) {
          int32_t rpc_index = map_pair.first;
          const RpcPerformanceLog& rpc_perf_log = map_pair.second;
          if (rpc_perf_log.successful_rpc_samples().empty() &&
              rpc_perf_log.failed_rpc_samples().empty())
            continue;
          auto& output_peer_log =
              (*log.mutable_peer_logs())[peers_[i][j].log_name];
          auto& output_rpc_logs = *output_peer_log.mutable_rpc_logs();
          output_rpc_logs[rpc_index].MergeFrom(rpc_perf_log);
        }
      }
    }
  }
  for (const auto& error : actionlist_error_dictionary_->GetContents()) {
    log.mutable_error_dictionary()->add_error_message(error);
  }
  AddActivityLogs(&log);
  return log;
}

// Process the incoming RPC;
// if have_dedicated_thread == true; all the processing is performed inline
// and the function returned is always empty,
// if have_dedicated_thread == false, only short RPCs are performed inline,
// and longer RPCs will return a non-empty function that the protocol driver
// should process in a seperate thread.
std::function<void()> DistBenchEngine::RpcHandler(ServerRpcState* state) {
  CHECK(state->request->has_rpc_index());
  if (canceled_.HasBeenNotified()) {
    absl::MutexLock m(&cancelation_mutex_);
    // Avoid reporting errors during the grace period:
    if (clock_->Now() > cancelation_time_) {
      state->response.set_error_message(
          absl::StrCat("Traffic cancelled: ", cancelation_reason_));
      state->SendResponseIfSet();
      state->FreeStateIfSet();
      return std::function<void()>();
    }
  }
  const auto& server_rpc = server_rpc_table_[state->request->rpc_index()];
  const auto& rpc_def = server_rpc.rpc_definition;

  if (state->request->has_response_payload_size()) {
    state->response.set_payload(
        std::string(state->request->response_payload_size(), 'D'));
  } else {
    state->response.set_payload(
        std::string(rpc_def.response_payload_size, 'D'));
  }

  int handler_action_list_index = server_rpc.handler_action_list_index;
  if (handler_action_list_index == -1) {
    state->SendResponseIfSet();
    state->FreeStateIfSet();
    return std::function<void()>();
  }

  if (state->have_dedicated_thread) {
    RunActionList(handler_action_list_index, state);
    return std::function<void()>();
  }

  ++detached_actionlist_threads_;
  return [=]() {
    RunActionList(handler_action_list_index, state);
    --detached_actionlist_threads_;
  };
}

void DistBenchEngine::RunActionList(int list_index,
                                    ServerRpcState* incoming_rpc_state,
                                    bool force_warmup) {
  CHECK_LT(static_cast<size_t>(list_index), action_lists_.size());
  CHECK_GE(list_index, 0);
  ActionListState s = {};
  s.actionlist_invocation = atomic_fetch_add_explicit(
      &actionlist_invocation_counts[list_index], 1, std::memory_order_relaxed);
  s.actionlist_index = list_index;
  s.actionlist_error_dictionary_ = actionlist_error_dictionary_;
  s.warmup_ = force_warmup || incoming_rpc_state->request->warmup();
  s.incoming_rpc_state = incoming_rpc_state;
  s.action_list = &action_lists_[list_index];
  bool sent_response_early = false;
  unsigned int seed =
      std::chrono::system_clock::now().time_since_epoch().count();
  std::mt19937 rand_gen(seed);

  // Allocate peer_logs_ for performance gathering, if needed:
  if (s.action_list->has_rpcs) {
    s.packed_samples_size_ = s.action_list->proto.max_rpc_samples();
    if (s.action_list->proto.max_rpc_samples() < 0) {
      s.packed_samples_size_ = 0;
    }
    s.packed_samples_.reset(new PackedLatencySample[s.packed_samples_size_]);
    s.remaining_initial_samples_ = s.packed_samples_size_;
    absl::MutexLock m(&s.action_mu);
    s.peer_logs_.resize(peers_.size());
    for (size_t i = 0; i < peers_.size(); ++i) {
      s.peer_logs_[i].resize(peers_[i].size());
    }
  }

  int size = s.action_list->proto.action_names_size();
  s.finished_action_indices.reserve(size);
  s.state_table = std::make_unique<ActionState[]>(size);
  while (true) {
    absl::Time now = clock_->Now();
    for (int i = 0; i < size; ++i) {
      if (s.state_table[i].started) {
        if (s.state_table[i].next_iteration_time < now) {
          if (s.state_table[i].action->proto.has_activity_config_name()) {
            RunActivity(&s.state_table[i]);
          } else {
            StartOpenLoopIteration(&s.state_table[i]);
          }
        }
        continue;
      }
      auto deps = s.action_list->list_actions[i].dependent_action_indices;
      bool deps_ready = true;
      for (const auto& dep : deps) {
        if (!s.state_table[dep].finished) {
          deps_ready = false;
          break;
        }
      }
      if (!deps_ready) continue;
      s.state_table[i].action_index = i;
      s.state_table[i].started = true;
      s.state_table[i].action = &s.action_list->list_actions[i];
      s.state_table[i].rand_gen = &rand_gen;
      if ((!sent_response_early && incoming_rpc_state) &&
          ((size == 1) ||
           s.state_table[i].action->proto.send_response_when_done())) {
        sent_response_early = true;
        s.state_table[i].all_done_callback = [&s, i, incoming_rpc_state,
                                              this]() {
          incoming_rpc_state->SendResponseIfSet();
          if (s.state_table[i].action->proto.cancel_traffic_when_done()) {
            CancelTraffic(absl::CancelledError("cancel_traffic_when_done"),
                          absl::Seconds(1));
          }
          s.FinishAction(i);
        };
      } else {
        s.state_table[i].all_done_callback = [&s, i, this]() {
          if (s.state_table[i].action->proto.cancel_traffic_when_done()) {
            CancelTraffic(absl::CancelledError("cancel_traffic_when_done"),
                          absl::Seconds(1));
          }
          s.FinishAction(i);
        };
      }
      s.state_table[i].action_list_state = &s;
      atomic_fetch_add_explicit(&s.pending_action_count_, 1,
                                std::memory_order_relaxed);
      InitiateAction(&s.state_table[i]);
    }
    absl::Time next_iteration_time = absl::InfiniteFuture();
    bool done = true;
    for (int i = 0; i < size; ++i) {
      absl::MutexLock m(&s.state_table[i].iteration_mutex);
      if (!s.state_table[i].finished) {
        if (s.state_table[i].next_iteration_time < next_iteration_time) {
          next_iteration_time = s.state_table[i].next_iteration_time;
        }
        done = false;
      }
    }
    if (done) break;
    auto some_actions_finished = [&s]() { return s.DidSomeActionsFinish(); };

    // Idle here till some actions are finished.
    if (clock_->MutexLockWhenWithDeadline(
            &s.action_mu, absl::Condition(&some_actions_finished),
            next_iteration_time)) {
      s.HandleFinishedActions();
    }
    s.action_mu.Unlock();
    if (canceled_.HasBeenNotified()) {
      LOG(INFO) << engine_name_ << ": Cancelled action list '"
                << s.action_list->proto.name() << "'";

      s.CancelActivities();
      s.WaitForAllPendingActions();
      break;
    }
  }
  if (incoming_rpc_state) {
    if (!sent_response_early) {
      incoming_rpc_state->SendResponseIfSet();
    }
    incoming_rpc_state->FreeStateIfSet();
  }
  // Merge the per-action-list logs into the overall logs:
  if (s.action_list->has_rpcs) {
    s.UnpackLatencySamples();
    absl::MutexLock m(&s.action_mu);
    for (size_t i = 0; i < s.peer_logs_.size(); ++i) {
      for (size_t j = 0; j < s.peer_logs_[i].size(); ++j) {
        if (s.peer_logs_[i][j].rpc_logs().empty()) continue;
        absl::MutexLock m(&peers_[i][j].mutex);
        peers_[i][j].partial_logs.emplace_back(std::move(s.peer_logs_[i][j]));
      }
    }
  }

  absl::MutexLock m(&cumulative_activity_log_mu_);
  s.UpdateActivitiesLog(&cumulative_activity_logs_);
}

void DistBenchEngine::ActionListState::FinishAction(int action_index) {
  action_mu.Lock();
  finished_action_indices.push_back(action_index);
  atomic_fetch_sub_explicit(&pending_action_count_, 1,
                            std::memory_order_relaxed);
  action_mu.Unlock();
}

bool DistBenchEngine::ActionListState::DidSomeActionsFinish() {
  int pending_actions =
      atomic_load_explicit(&pending_action_count_, std::memory_order_relaxed);
  return !pending_actions || !finished_action_indices.empty();
};

void DistBenchEngine::ActionListState::HandleFinishedActions() {
  if (!DidSomeActionsFinish()) {
    LOG(FATAL) << "finished_action_indices is empty";
  }
  for (const auto& finished_action_index : finished_action_indices) {
    state_table[finished_action_index].finished = true;
    state_table[finished_action_index].next_iteration_time =
        absl::InfiniteFuture();
  }
  finished_action_indices.clear();
}

void DistBenchEngine::ActionListState::CancelActivities() {
  bool finished_some_actions = false;
  for (int i = 0; i < action_list->proto.action_names_size(); ++i) {
    auto action_state = &state_table[i];
    if (action_state->finished) {
      continue;
    }
    if (action_state->action->proto.has_activity_config_name()) {
      finished_some_actions = true;
#ifdef NDEBUG
      action_state->all_done_callback();
#else
      auto adcb = std::move(action_state->all_done_callback);
      action_state->all_done_callback = []() {
        LOG(FATAL) << "all_done_callback already called!";
      };
      adcb();
#endif
    }
  }
  if (finished_some_actions) {
    absl::MutexLock m(&action_mu);
    HandleFinishedActions();
  }
}

// Updates the activities_log_ map with the activity metrics from the
// activities. In case two activities have same ActivityConfig, the metrics from
// these activities are summed up into one metric.
// For example:
// The 'iteration_count' for two activities 'A1' and 'A2' that have the same
// activity config 'AC' and have run 20 and 30 times respectively is reported as
// iteration_count = 50.
void DistBenchEngine::ActionListState::UpdateActivitiesLog(
    std::map<std::string, std::map<std::string, int64_t>>*
        cumulative_activity_logs) {
  for (int i = 0; i < action_list->proto.action_names_size(); ++i) {
    auto action_state = &state_table[i];
    if (action_state->action->proto.has_activity_config_name()) {
      auto activity_config_name =
          action_state->action->proto.activity_config_name();
      auto new_log = action_state->activity->GetActivityLog();

      for (auto new_metrics_it = new_log.activity_metrics().begin();
           new_metrics_it != new_log.activity_metrics().end();
           new_metrics_it++) {
        (*cumulative_activity_logs)[activity_config_name]
                                   [new_metrics_it->name()] +=
            new_metrics_it->value_int();
      }
    }
  }
}

void DistBenchEngine::ActionListState::WaitForAllPendingActions() {
  auto some_actions_finished = [this]() { return DidSomeActionsFinish(); };
  bool done;
  do {
    action_mu.LockWhen(absl::Condition(&some_actions_finished));
    HandleFinishedActions();
    done = true;
    for (int i = 0; i < action_list->proto.action_names_size(); ++i) {
      const auto& state = state_table[i];
      if (state.started && !state.finished) {
        done = false;
        break;
      }
    }
    action_mu.Unlock();
  } while (!done);
}

void DistBenchEngine::ActionListState::UnpackLatencySamples() {
  absl::MutexLock m(&action_mu);
  if (packed_sample_number_ <= packed_samples_size_) {
    packed_samples_size_ = packed_sample_number_;
  } else {
    // If we did Reservoir sampling, we should sort the data:
    std::sort(packed_samples_.get(),
              packed_samples_.get() + packed_samples_size_);
  }
  for (size_t i = 0; i < packed_samples_size_; ++i) {
    const auto& packed_sample = packed_samples_[i];
    CHECK_LT(packed_sample.service_type, peer_logs_.size());
    auto& service_log = peer_logs_[packed_sample.service_type];
    CHECK_LT(packed_sample.instance, service_log.size());
    auto& peer_log = service_log[packed_sample.instance];
    auto& rpc_log = (*peer_log.mutable_rpc_logs())[packed_sample.rpc_index];
    auto* sample = packed_sample.success ? rpc_log.add_successful_rpc_samples()
                                         : rpc_log.add_failed_rpc_samples();
    sample->set_request_size(packed_sample.request_size);
    sample->set_response_size(packed_sample.response_size);
    sample->set_start_timestamp_ns(packed_sample.start_timestamp_ns);
    sample->set_latency_ns(packed_sample.latency_ns);
    if (packed_sample.latency_weight) {
      sample->set_latency_weight(packed_sample.latency_weight);
    }
    if (packed_sample.trace_context) {
      *sample->mutable_trace_context() = *packed_sample.trace_context;
    }
    if (packed_sample.warmup) {
      sample->set_warmup(true);
    }
    if (packed_sample.error_index) {
      sample->set_error_index(packed_sample.error_index);
    }
  }
}

void DistBenchEngine::ActionListState::RecordLatency(size_t rpc_index,
                                                     size_t service_type,
                                                     size_t instance,
                                                     ClientRpcState* state) {
  // If we are using packed samples we avoid grabbing a mutex, but are limited
  // in how many samples total we can collect:
  if (packed_samples_size_) {
    const size_t sample_number = atomic_fetch_add_explicit(
        &packed_sample_number_, 1UL, std::memory_order_relaxed);
    size_t index = sample_number;
    if (index < packed_samples_size_) {
      RecordPackedLatency(sample_number, index, rpc_index, service_type,
                          instance, state);
      atomic_fetch_sub_explicit(&remaining_initial_samples_, 1UL,
                                std::memory_order_release);
      return;
    }
    // Simple Reservoir Sampling:
    absl::BitGen bitgen;
    index = absl::Uniform(absl::IntervalClosedClosed, bitgen, 0UL, index);
    if (index >= packed_samples_size_) {
      // Histogram per [rpc_index, service] would be ideal here:
      // Also client rpc state could point to the destination stats instead
      // of requiring us to look them up below.
      // dropped_rpc_count_ += 1;
      // dropped_rpc_total_latency_ += latency
      // dropped_rpc_request_size_ += state->request.payload().size();
      // dropped_rpc_response_size_ += state->response.payload().size();
      return;
    }
    // Wait until all initial samples are done:
    while (atomic_load_explicit(&remaining_initial_samples_,
                                std::memory_order_acquire)) {
    }
    // Reservoir samples are serialized.
    absl::MutexLock m(&reservoir_sample_lock_);
    PackedLatencySample& packed_sample = packed_samples_[index];
    if (packed_sample.sample_number < sample_number) {
      // Without arena allocation, via sample_arena_ we would need to do:
      // delete packed_sample.trace_context;
      RecordPackedLatency(sample_number, index, rpc_index, service_type,
                          instance, state);
    }
    return;
  }

  // Without packed samples we can support any number of samples, but then we
  // also have to grab a mutex for each sample, and may have to grow the
  // underlying array while holding the mutex.
  absl::MutexLock m(&action_mu);
  CHECK_LT(service_type, peer_logs_.size());
  auto& service_log = peer_logs_[service_type];
  CHECK_LT(instance, service_log.size());
  auto& peer_log = service_log[instance];
  auto& rpc_log = (*peer_log.mutable_rpc_logs())[rpc_index];
  auto* sample = state->success ? rpc_log.add_successful_rpc_samples()
                                : rpc_log.add_failed_rpc_samples();
  auto latency = state->end_time - state->start_time;
  sample->set_start_timestamp_ns(absl::ToUnixNanos(state->start_time));
  sample->set_latency_ns(absl::ToInt64Nanoseconds(latency));
  if (state->prior_start_time != absl::InfinitePast()) {
    sample->set_latency_weight(
        absl::ToInt64Nanoseconds(state->start_time - state->prior_start_time));
  }
  sample->set_request_size(state->request.payload().size());
  sample->set_response_size(state->response.payload().size());
  if (state->request.warmup()) {
    sample->set_warmup(true);
  }
  if (!state->request.trace_context().engine_ids().empty()) {
    *sample->mutable_trace_context() =
        std::move(state->request.trace_context());
  }
  if (!state->response.error_message().empty()) {
    sample->set_error_index(actionlist_error_dictionary_->GetIndex(
        state->response.error_message()));
  }
}

void DistBenchEngine::ActionListState::RecordPackedLatency(
    size_t sample_number, size_t index, size_t rpc_index, size_t service_type,
    size_t instance, ClientRpcState* state) {
  PackedLatencySample& packed_sample = packed_samples_[index];
  packed_sample.error_index = 0;
  if (!state->response.error_message().empty()) {
    packed_sample.error_index =
        actionlist_error_dictionary_->GetIndex(state->response.error_message());
  }
  packed_sample.sample_number = sample_number;
  packed_sample.trace_context = nullptr;
  packed_sample.rpc_index = rpc_index;
  packed_sample.service_type = service_type;
  packed_sample.instance = instance;
  packed_sample.success = state->success;
  packed_sample.warmup = state->request.warmup();
  auto latency = state->end_time - state->start_time;
  packed_sample.start_timestamp_ns = absl::ToUnixNanos(state->start_time);
  packed_sample.latency_ns = absl::ToInt64Nanoseconds(latency);
  packed_sample.latency_weight = 0;
  if (state->prior_start_time != absl::InfinitePast()) {
    packed_sample.latency_weight =
        absl::ToInt64Nanoseconds(state->start_time - state->prior_start_time);
  }
  packed_sample.request_size = state->request.payload().size();
  packed_sample.response_size = state->response.payload().size();
  if (!state->request.trace_context().engine_ids().empty()) {
    packed_sample.trace_context =
        ::google::protobuf::Arena::CreateMessage<TraceContext>(&sample_arena_);
    *packed_sample.trace_context = state->request.trace_context();
  }
}

void DistBenchEngine::InitiateAction(ActionState* action_state) {
  auto& action = *action_state->action;
  if (action.actionlist_index >= 0) {
    std::shared_ptr<const GenericRequest> copied_request =
        std::make_shared<GenericRequest>(
            *action_state->action_list_state->incoming_rpc_state->request);
    int action_list_index = action.actionlist_index;
    action_state->iteration_function =
        [this, action_list_index, copied_request](
            std::shared_ptr<ActionIterationState> iteration_state) {
          ServerRpcState* copied_server_rpc_state = new ServerRpcState{};
          copied_server_rpc_state->request = copied_request.get();
          copied_server_rpc_state->have_dedicated_thread = true;
          copied_server_rpc_state->SetFreeStateFunction(
              [=] { delete copied_server_rpc_state; });
          thread_pool_->AddTask([this, action_list_index, iteration_state,
                                 copied_request,
                                 copied_server_rpc_state]() mutable {
            RunActionList(action_list_index, copied_server_rpc_state,
                          iteration_state->warmup);
            FinishIteration(iteration_state);
          });
        };
  } else if (action.rpc_service_index >= 0) {
    CHECK_LT(static_cast<size_t>(action.rpc_service_index), peers_.size());
    int rpc_service_index = action.rpc_service_index;
    CHECK_GE(rpc_service_index, 0);
    CHECK_LT(static_cast<size_t>(rpc_service_index), peers_.size());

    if (peers_[rpc_service_index].empty()) return;

    action_state->rpc_index = action.rpc_index;
    action_state->rpc_service_index = rpc_service_index;
    action_state->iteration_function =
        [this](std::shared_ptr<ActionIterationState> iteration_state) {
          RunRpcActionIteration(iteration_state);
        };
  } else if (action.proto.has_activity_config_name()) {
    auto* config = &stored_activity_config_[action.activity_config_index];
    action_state->activity = AllocateActivity(config, clock_);
    action_state->iteration_function =
        [this,
         action_state](std::shared_ptr<ActionIterationState> iteration_state) {
          action_state->activity->DoActivity();
          FinishIteration(iteration_state);
        };
  } else {
    LOG(FATAL) << "Supporting only RPCs and Activities as of now.";
  }

  bool open_loop = false;
  int64_t max_iterations = 1;
  absl::Time time_limit = absl::InfiniteFuture();
  if (action.proto.has_iterations()) {
    if (action.proto.iterations().has_max_iteration_count()) {
      max_iterations = action.proto.iterations().max_iteration_count();
    } else {
      max_iterations = std::numeric_limits<int64_t>::max();
    }
    if (action.proto.iterations().has_max_duration_us()) {
      time_limit =
          clock_->Now() +
          absl::Microseconds(action.proto.iterations().max_duration_us());
    }
    open_loop = action.proto.iterations().has_open_loop_interval_ns();
  }
  if (max_iterations < 1) {
    LOG(WARNING) << "an action had a weird number of iterations";
  }
  action_state->iteration_mutex.Lock();
  action_state->iteration_limit = max_iterations;
  action_state->time_limit = time_limit;
  action_state->next_iteration = 0;
  if (action_state->action->proto.has_activity_config_name()) {
    action_state->next_iteration_time = absl::InfinitePast();
  }
  action_state->iteration_mutex.Unlock();

  if (open_loop) {
    CHECK_EQ(action_state->next_iteration_time, absl::InfiniteFuture());
    absl::Duration period = absl::Nanoseconds(
        action_state->action->proto.iterations().open_loop_interval_ns());
    auto& interval_distribution = action_state->action->proto.iterations()
                                      .open_loop_interval_distribution();
    if (interval_distribution == "sync_burst") {
      absl::Duration start = clock_->Now() - absl::UnixEpoch();
      action_state->next_iteration_time =
          period + absl::UnixEpoch() + absl::Floor(start, period);
    } else if (interval_distribution == "sync_burst_spread") {
      absl::Duration start = clock_->Now() - absl::UnixEpoch();
      double nb_peers = peers_[action_state->rpc_service_index].size();
      double fraction = service_instance_ / nb_peers;
      LOG(INFO) << "sync_burst_spread burst delay: " << fraction * period;
      action_state->next_iteration_time = period + absl::UnixEpoch() +
                                          absl::Floor(start, period) +
                                          fraction * period;
    } else {
      action_state->interval_is_exponential =
          (interval_distribution == "exponential");
      action_state->next_iteration_time = clock_->Now();
    }
    // StartOpenLoopIteration will be called from RunActionList().
  } else {
    int64_t parallel_copies = std::min(
        action.proto.iterations().max_parallel_iterations(), max_iterations);
    action_state->iteration_mutex.Lock();
    action_state->next_iteration = parallel_copies;
    action_state->iteration_mutex.Unlock();
    for (int i = 0; i < parallel_copies; ++i) {
      auto it_state = std::make_shared<ActionIterationState>();
      it_state->action_state = action_state;
      it_state->iteration_number = i;
      StartIteration(it_state);
    }
  }
}

void DistBenchEngine::StartOpenLoopIteration(ActionState* action_state) {
  absl::Duration period = absl::Nanoseconds(
      action_state->action->proto.iterations().open_loop_interval_ns());
  auto it_state = std::make_shared<ActionIterationState>();
  it_state->action_state = action_state;
  action_state->iteration_mutex.Lock();
  if (action_state->interval_is_exponential) {
    action_state->next_iteration_time +=
        period * action_state->exponential_gen(*action_state->rand_gen);
  } else {
    action_state->next_iteration_time += period;
  }
  if (action_state->next_iteration_time > action_state->time_limit) {
    action_state->next_iteration_time = absl::InfiniteFuture();
  }
  it_state->iteration_number = action_state->next_iteration++;
  action_state->iteration_mutex.Unlock();
  StartIteration(it_state);
}

void DistBenchEngine::RunActivity(ActionState* action_state) {
  auto it_state = std::make_shared<ActionIterationState>();
  it_state->action_state = action_state;
  StartIteration(it_state);
}

void DistBenchEngine::FinishIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  ActionState* state = iteration_state->action_state;
  bool open_loop =
      state->action->proto.iterations().has_open_loop_interval_ns();
  bool done = canceled_.HasBeenNotified();
  state->iteration_mutex.Lock();
  ++state->finished_iterations;
  if (state->next_iteration == state->iteration_limit) {
    done = true;
  } else if (!open_loop) {
    // Closed loop iteration:
    if (state->time_limit != absl::InfiniteFuture()) {
      if (clock_->Now() > state->time_limit) {
        done = true;
      }
    }
  } else {
    // Open loop (possibly sync_burst) iteration:
    if (state->next_iteration_time > state->time_limit) {
      done = true;
    }
  }
  const bool start_another_iteration = !done && !open_loop;
  if (start_another_iteration) {
    iteration_state->iteration_number = state->next_iteration++;
  }
  int pending_iterations = state->next_iteration - state->finished_iterations;
  state->iteration_mutex.Unlock();
  if (done && !pending_iterations) {
#ifdef NDEBUG
    state->all_done_callback();
#else
    auto adcb = std::move(state->all_done_callback);
    state->all_done_callback = []() {
      LOG(FATAL) << "all_done_callback already called!";
    };
    adcb();
#endif
  } else if (!state->action->proto.has_activity_config_name() &&
             start_another_iteration) {
    StartIteration(iteration_state);
  }
}

void DistBenchEngine::StartIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  ActionState* action_state = iteration_state->action_state;
  iteration_state->warmup =
      action_state->action_list_state->warmup_ ||
      (iteration_state->iteration_number <
       action_state->action->proto.iterations().warmup_iterations());
  action_state->iteration_function(iteration_state);
}

// This works fine for 1-at-a-time closed-loop iterations:
void DistBenchEngine::RunRpcActionIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  ActionState* action_state = iteration_state->action_state;
  // Pick the subset of the target service instances to fanout to:
  std::vector<int> current_targets = PickRpcFanoutTargets(action_state);
  if (current_targets.empty()) {
    // LOG(INFO) << "targets is empty";
    FinishIteration(iteration_state);
    return;
  }
  iteration_state->rpc_states.resize(current_targets.size());
  iteration_state->remaining_rpcs = current_targets.size();

  // Setup tracing:
  const int rpc_index = action_state->rpc_index;
  const auto& rpc_def = client_rpc_table_[rpc_index].rpc_definition;
  const auto& rpc_spec = rpc_def.rpc_spec;
  int trace_count = client_rpc_table_[rpc_index].rpc_tracing_counter++;
  bool do_trace = (rpc_spec.tracing_interval() > 0) &&
                  !(trace_count % rpc_spec.tracing_interval());
  GenericRequest common_request;
  const ServerRpcState* const incoming_rpc_state =
      action_state->action_list_state->incoming_rpc_state;
  if (incoming_rpc_state->request->has_trace_context()) {
    do_trace = true;
    *common_request.mutable_trace_context() =
        incoming_rpc_state->request->trace_context();
  }
  if (do_trace) {
    common_request.mutable_trace_context()->add_engine_ids(trace_id_);
    common_request.mutable_trace_context()->add_actionlist_invocations(
        action_state->action_list_state->actionlist_invocation);
    common_request.mutable_trace_context()->add_actionlist_indices(
        action_state->action_list_state->actionlist_index);
    common_request.mutable_trace_context()->add_action_indices(
        action_state->action_index);
    common_request.mutable_trace_context()->add_action_iterations(
        iteration_state->iteration_number);
  }
  common_request.set_rpc_index(rpc_index);
  common_request.set_warmup(iteration_state->warmup);

  if (rpc_def.sample_generator_index == -1) {
    common_request.set_payload(std::string(rpc_def.request_payload_size, 'D'));
  } else {
    // This RPC uses a distribution of sizes.
    auto sample = sample_generator_array_[rpc_def.sample_generator_index]
                      ->GetRandomSample(action_state->rand_gen);

    common_request.set_payload(
        std::string(sample[kRequestPayloadSizeField], 'D'));

    if (sample.size() > kResponsePayloadSizeField) {
      common_request.set_response_payload_size(
          sample[kResponsePayloadSizeField]);
    } else {
      // Only a 1D distribution, therefore we should also use the request size
      // as the response size.
      common_request.set_response_payload_size(
          sample[kRequestPayloadSizeField]);
    }
  }

  const int rpc_service_index = action_state->rpc_service_index;
  const auto& servers = peers_[rpc_service_index];
  for (size_t i = 0; i < current_targets.size(); ++i) {
    int peer_instance = current_targets[i];
    ++pending_rpcs_;
    ClientRpcState* rpc_state;
    {
      absl::MutexLock m(&peers_[rpc_service_index][peer_instance].mutex);
      rpc_state = &iteration_state->rpc_states[i];
      rpc_state->request = common_request;
      if (do_trace) {
        rpc_state->request.mutable_trace_context()->add_fanout_index(i);
      }
#ifndef NDEBUG
      CHECK_EQ(
          rpc_state->request.trace_context().engine_ids().size(),
          rpc_state->request.trace_context().actionlist_invocations().size());
      CHECK_EQ(rpc_state->request.trace_context().engine_ids().size(),
               rpc_state->request.trace_context().actionlist_indices().size());
      CHECK_EQ(rpc_state->request.trace_context().engine_ids().size(),
               rpc_state->request.trace_context().action_indices().size());
      CHECK_EQ(rpc_state->request.trace_context().engine_ids().size(),
               rpc_state->request.trace_context().action_iterations().size());
      CHECK_EQ(rpc_state->request.trace_context().engine_ids().size(),
               rpc_state->request.trace_context().fanout_index().size());
      // CHECK_EQ(0, rpc_state->request.trace_context().iterations().size());
#endif
    }  // End of MutexLock m
    rpc_state->prior_start_time = rpc_state->start_time;
    rpc_state->start_time = clock_->Now();
    pd_->InitiateRpc(
        servers[peer_instance].pd_id, rpc_state,
        [this, rpc_state, iteration_state, peer_instance]() mutable {
          ActionState* action_state = iteration_state->action_state;
          rpc_state->end_time = clock_->Now();
          if (!rpc_state->response.error_message().empty()) {
            rpc_state->success = false;
          }
          if (absl::StartsWith(rpc_state->response.error_message(),
                               "Traffic cancelled: RESOURCE_EXHAUSTED:")) {
            CancelTraffic(absl::UnknownError(absl::StrCat(
                "Peer reported ", rpc_state->response.error_message())));
          }
          action_state->action_list_state->RecordLatency(
              action_state->rpc_index, action_state->rpc_service_index,
              peer_instance, rpc_state);
          if (--iteration_state->remaining_rpcs == 0) {
            FinishIteration(iteration_state);
          }
          --pending_rpcs_;
        });
    if (pending_rpcs_ > traffic_config_.overload_limits().max_pending_rpcs()) {
      CancelTraffic(absl::ResourceExhaustedError("Too many RPCs pending"));
    }
  }
}

std::vector<int> DistBenchEngine::PickLinearTargets(FanoutFilter filter, int distance, const ServiceSpec&  peer_service) {
  int x = ranks_.x;
  int y = ranks_.y;
  int z = ranks_.z;
  switch(filter) {
    case kLinearX:
      x += distance;
      break;

    case kLinearY:
      y += distance;
      break;

    case kLinearZ:
      z += distance;
      break;

    default:
      break;
  }
  if (x < 0 || y < 0 || z < 0) {
    return {};
  }
  if (x >= peer_service.x_size() || y >= peer_service.y_size() || z >= peer_service.z_size()) {
    return {};
  }
  return {x + y * peer_service.x_size() + z * peer_service.x_size() * peer_service.y_size()};
}

std::vector<int> DistBenchEngine::PickRingTargets(
    FanoutFilter filter, const ServiceSpec& peer_service) {
  int x = ranks_.x + peer_service.x_size();
  int y = ranks_.y + peer_service.y_size();
  int z = ranks_.z + peer_service.z_size();
  switch(filter) {
    case kRingX:
      x++;
      break;

    case kRingY:
      y++;
      break;

    case kRingZ:
      z++;
      break;

    case kAlternatingRingX:
      if (ranks_.y & 0x1) {
        x--;
      } else {
        x++;
      }
      break;

    case kAlternatingRingY:
      if (ranks_.x & 0x1) {
        y--;
      } else {
        y++;
      }
      break;

    case kAlternatingRingZ:
      if (ranks_.x & 0x1) {
        z--;
      } else {
        z++;
      }
      break;

    default:
      break;
  }
  x %= peer_service.x_size();
  y %= peer_service.y_size();
  z %= peer_service.z_size();
  return {x + y * peer_service.x_size() + z * peer_service.x_size() * peer_service.y_size()};
}

std::vector<int> DistBenchEngine::PickRankTargets(
    FanoutFilter filter, const ServiceSpec& peer_service) {
  int x_size = peer_service.x_size();
  int y_size = peer_service.y_size();
  int z_size = peer_service.z_size();
  // LOG(INFO) << "sizes : " << x_size << " " << y_size << " " << z_size;
  int x_start = 0;
  int y_start = 0;
  int z_start = 0;
  int x_end = x_size;
  int y_end = y_size;
  int z_end = z_size;
  if (filter & kSameX) {
    x_start = ranks_.x;
    x_end = x_start + 1;
  }
  if (filter & kSameY) {
    y_start = ranks_.y;
    y_end = y_start + 1;
  }
  if (filter & kSameZ) {
    z_start = ranks_.z;
    z_end = z_start + 1;
  }
  std::vector<int> half_ret;
  std::vector<int> ret;
  size_t ret_size = (x_end - x_start) * (y_end - y_start) * (z_end - z_start);
  ret.reserve(ret_size);
  for (int i = x_start; i < x_end; ++i) {
    for (int j = y_start; j < y_end; ++j) {
      for (int k = z_start; k < z_end; ++k) {
        int target = i + j * x_size + k * x_size * y_size;
        ret.push_back(target);
        if (target == service_instance_) {
          half_ret = std::move(ret);
          ret.clear();
          ret.reserve(ret_size);
        }
      }
    }
  }
  // This gives each node a unique order in which it sends RPCs to its peers.
  // otherwise node zero would get incoming requests all at once, while node
  // N-1 would get none for the begining of a burst. In general, node N will
  // start by sending to nodes N + 1, N + 2, N + 3, before wrapping around to 
  // nodes 0, 1, 2, and ending at node N - 1.
  ret.insert(ret.end(), half_ret.begin(), half_ret.end());
  return ret;
}

// Return a vector of service instances, which have to be translated to
// protocol_drivers endpoint ids by the caller.
std::vector<int> DistBenchEngine::PickRpcFanoutTargets(
    ActionState* action_state) {
  bool exclude_self = action_state->rpc_service_index == service_index_;
  const int rpc_index = action_state->rpc_index;
  const auto& rpc_def = client_rpc_table_[rpc_index].rpc_definition;
  std::vector<int> targets;
  int num_servers = peers_[action_state->rpc_service_index].size();

  switch (rpc_def.fanout_filter) {
    default:
      // Default case: return the first instance of the service
      targets.reserve(1);
      targets.push_back(0);
      break;

    case kAll:
    case kSameX:
    case kSameY:
    case kSameZ:
    case kSameXY:
    case kSameXZ:
    case kSameYZ:
    case kSameXYZ:
    targets = PickRankTargets(rpc_def.fanout_filter, rpc_def.server_service_spec);
      break;

    case kLinearX:
    case kLinearY:
    case kLinearZ:
      targets = PickLinearTargets(rpc_def.fanout_filter, rpc_def.fanout_filter_distance, rpc_def.server_service_spec);
      break;

    case kRingX:
    case kRingY:
    case kRingZ:
    case kAlternatingRingX:
    case kAlternatingRingY:
    case kAlternatingRingZ:
    targets = PickRingTargets(rpc_def.fanout_filter, rpc_def.server_service_spec);
      break;

    case kRandomSingle:
      targets.reserve(1);
      targets.push_back(random() % num_servers);
      break;

    case kRoundRobin:
      targets.reserve(1);
      targets.push_back(client_rpc_table_[rpc_index].rpc_tracing_counter %
                        num_servers);
      break;

    case kStochastic:
      std::map<int, std::vector<int>> partial_rand_vects =
          action_state->partially_randomized_vectors;

      int nb_targets = 0;
      float random_val = absl::Uniform(random_generator, 0, 1.0);
      float cur_val = 0.0;
      for (const auto& d : rpc_def.stochastic_dist) {
        cur_val += d.probability;
        if (random_val <= cur_val) {
          nb_targets = d.nb_targets;
          break;
        }
      }
      if (nb_targets > num_servers) {
        nb_targets = num_servers;
      }

      // Generate a vector to pick random targets from (only done once)
      partial_rand_vects.try_emplace(num_servers, std::vector<int>());
      std::vector<int>& from_vector = partial_rand_vects[num_servers];
      if (from_vector.empty()) {
        for (int i = 0; i < num_servers; i++) {
          from_vector.push_back(i);
        }
      }

      // Randomize and pick up to nb_targets
      for (int i = 0; i < nb_targets; i++) {
        int rnd_pos = i + (random() % (num_servers - i));
        std::swap(from_vector[i], from_vector[rnd_pos]);
        int target = from_vector[i];
        CHECK_NE(target, -1);
        targets.push_back(target);
      }
      break;
  }

  if (exclude_self) {
    targets.erase(std::remove(targets.begin(), targets.end(), service_instance_), targets.end());

  }

  return targets;
}

int DistBenchEngine::GetSampleGeneratorIndex(
    const std::string& distribution_config_name) {
  const auto& dc_index_it =
      sample_generator_indices_map_.find(distribution_config_name);
  if (dc_index_it == sample_generator_indices_map_.end()) {
    LOG(WARNING) << "Unknown distribution_config_name: '"
                 << distribution_config_name << "' provided.";
    return -1;
  }
  return dc_index_it->second;
}

absl::Status DistBenchEngine::AllocateAndInitializeSampleGenerators() {
  for (int i = 0; i < traffic_config_.distribution_config_size(); ++i) {
    const auto& config = traffic_config_.distribution_config(i);
    const auto& config_name = config.name();

    if (sample_generator_indices_map_.find(config_name) ==
        sample_generator_indices_map_.end()) {
      auto maybe_canonical_config =
          GetCanonicalDistributionConfig(config, canonical_2d_fields);
      if (!maybe_canonical_config.ok()) {
        maybe_canonical_config =
            GetCanonicalDistributionConfig(config, canonical_1d_fields);
      }
      if (!maybe_canonical_config.ok()) {
        return maybe_canonical_config.status();
      }
      auto canonical_config = maybe_canonical_config.value();

      auto maybe_sample_generator = AllocateSampleGenerator(canonical_config);
      if (!maybe_sample_generator.ok()) return maybe_sample_generator.status();

      sample_generator_array_.push_back(
          std::move(maybe_sample_generator.value()));
      sample_generator_indices_map_[config_name] =
          sample_generator_array_.size() - 1;

    } else {
      return absl::FailedPreconditionError(
          absl::StrCat("Distribution config '", config_name,
                       "' was defined more than once."));
    }
  }
  return absl::OkStatus();
}

}  // namespace distbench
