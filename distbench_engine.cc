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

#include "distbench_engine.h"

#include "distbench_utils.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"

namespace distbench {

grpc::Status DistBenchEngine::SetupConnection(grpc::ServerContext* context,
                                              const ConnectRequest* request,
                                              ConnectResponse* response) {
  auto maybe_info = pd_->HandlePreConnect(request->initiator_info(), 0);
  if (maybe_info.ok()) {
    response->set_responder_info(maybe_info.value());
    return grpc::Status::OK;
  } else {
    return abslStatusToGrpcStatus(maybe_info.status());
  }
}

DistBenchEngine::DistBenchEngine(
    std::unique_ptr<ProtocolDriver> pd, SimpleClock* clock)
  : pd_(std::move(pd)) {
  clock_ = clock;
}

DistBenchEngine::~DistBenchEngine() {
  if (server_) {
    server_->Shutdown();
    server_->Wait();
  }
  if (pd_) {
    pd_->ShutdownServer();
    pd_->ShutdownClient();
  }
}

// Initialize the payload map and perform basic validation
absl::Status DistBenchEngine::InitializePayloadsMap() {
  for (int i = 0; i < traffic_config_.payload_descriptions_size(); ++i) {
    const auto& payload_spec = traffic_config_.payload_descriptions(i);
    const auto& payload_spec_name = payload_spec.name();

    // Check for double declaration
    if ( payload_map_.find(payload_spec_name) != payload_map_.end() )
      return absl::InvalidArgumentError(
          "Double definition of payload_descriptions: " + payload_spec_name);

    payload_map_[payload_spec_name] = payload_spec;
  }

  return absl::OkStatus();
}

int DistBenchEngine::get_payload_size(const std::string& payload_name) {
  const auto& payload = payload_map_[payload_name];
  int size = -1;

  if (payload.has_size()) {
    size = payload.size();
  } else {
    LOG(WARNING) << "No size defined for payload " << payload_name << "\n";
  }

  return size;
}

absl::Status DistBenchEngine::InitializeRpcDefinitionStochastic(
    RpcDefinition& rpc_def) {
  const auto& rpc_spec = rpc_def.rpc_spec;
  std::string fanout_filter = rpc_spec.fanout_filter();
  const std::string stochastic_keyword = "stochastic";

  rpc_def.is_stochastic_fanout = false;

  if (!absl::StartsWith(fanout_filter, stochastic_keyword))
    return absl::OkStatus();
  fanout_filter.erase(0, stochastic_keyword.length());

  if (!absl::StartsWith(fanout_filter, "{")) {
    return absl::InvalidArgumentError(
        "Invalid stochastic filter; should starts with stochastic{");
  }
  fanout_filter.erase(0, 1);  // Consume the '{'

  if (!absl::EndsWith(fanout_filter, "}")) {
    return absl::InvalidArgumentError(
        "Invalid stochastic filter; should ends with }");
  }
  fanout_filter.pop_back();  // Consume the '}'

  float total_probability = 0.;
  for (auto s : absl::StrSplit(fanout_filter, ',')) {
    std::vector<std::string> v = absl::StrSplit(s, ':');
    if (v.size() != 2)
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; only 1 : accepted");

    StochasticDist dist;
    if (!absl::SimpleAtof(v[0], &dist.probability))
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; unable to decode probability");
    if (dist.probability < 0 || dist.probability > 1)
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; probability should be between 0. and 1.");
    total_probability += dist.probability;

    if (!absl::SimpleAtoi(v[1], &dist.nb_targets))
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; unable to decode nb_targets");
    if (dist.nb_targets < 0)
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; nb_targets should be >= 0");

    rpc_def.stochastic_dist.push_back(dist);
  }

  if (total_probability != 1.0)
    LOG(WARNING) << "The probability for the stochastic fanout of "
                 << rpc_def.rpc_spec.name()
                 << " does not add up to 1.0 (is "
                 << total_probability << ")";

  if (rpc_def.stochastic_dist.empty()) {
      return absl::InvalidArgumentError(
          "Invalid stochastic filter; need at least a value pair");
  }

  rpc_def.is_stochastic_fanout = true;

  return absl::OkStatus();
}

absl::Status DistBenchEngine::InitializeRpcDefinitionsMap() {
  for (int i = 0; i < traffic_config_.rpc_descriptions_size(); ++i) {
    const auto& rpc_spec = traffic_config_.rpc_descriptions(i);
    const auto& rpc_name = rpc_spec.name();

    RpcDefinition rpc_def;
    rpc_def.rpc_spec = rpc_spec;

    // Get request payload size
    rpc_def.request_payload_size = -1;
    if (rpc_spec.has_request_payload_name()) {
      const auto& payload_name = rpc_spec.request_payload_name();
      rpc_def.request_payload_size = get_payload_size(payload_name);
    }
    if (rpc_def.request_payload_size == -1) {
      rpc_def.request_payload_size = 16;
      LOG(WARNING) << "No request payload defined for " << rpc_name <<
                 "; using a default of " <<
                 rpc_def.request_payload_size;
    }

    // Get response payload size
    rpc_def.response_payload_size = -1;
    if (rpc_spec.has_response_payload_name()) {
      const auto& payload_name = rpc_spec.response_payload_name();
      rpc_def.response_payload_size = get_payload_size(payload_name);
    }
    if (rpc_def.response_payload_size == -1) {
      rpc_def.response_payload_size = 32;
      LOG(WARNING) << "No response payload defined for " << rpc_name <<
                 "; using a default of " <<
                 rpc_def.response_payload_size;
    }

    auto ret = InitializeRpcDefinitionStochastic(rpc_def);
    if (!ret.ok())
      return ret;

    rpc_map_[rpc_name] = rpc_def;
  }

  return absl::OkStatus();
}

absl::Status DistBenchEngine::InitializeTables() {
  auto ret_init_payload = InitializePayloadsMap();
  if ( !ret_init_payload.ok() )
    return ret_init_payload;

  auto ret_init_rpc_def = InitializeRpcDefinitionsMap();
  if ( !ret_init_rpc_def.ok() )
    return ret_init_rpc_def;

  // Convert the action table to a map indexed by name:
  std::map<std::string, Action> action_map;
  for (int i = 0; i < traffic_config_.actions_size(); ++i) {
    const auto& action = traffic_config_.actions(i);
    action_map[action.name()] = traffic_config_.actions(i);
  }
  std::map<std::string, int> rpc_name_index_map =
    EnumerateRpcs(traffic_config_);
  std::map<std::string, int> service_index_map =
    EnumerateServiceTypes(traffic_config_);

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
        auto it3 = service_index_map.find(target_service_name);
        if (it3 == service_index_map.end()) {
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
      } else {
        LOG(FATAL) << "only rpc actions are supported for now";
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
      LOG(INFO) << rpc.ShortDebugString();
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
    server_rpc_table_[i].handler_action_list_index = it->second;
    server_rpc_table_[i].rpc_definition = rpc_map_[rpc.name()];

    auto it1 = service_index_map.find(server_service_name);
    if (it1 == service_index_map.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat(
            "Rpc ", rpc.name(), " specifies unknown server service_type ",
            server_service_name));
    }
    auto it2 = service_index_map.find(client_service_name);
    if (it2 == service_index_map.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat(
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
    std::string_view service_name,
    int service_instance,
    int* port) {
  traffic_config_ = global_description;
  CHECK(!service_name.empty());
  service_name_ = service_name;
  service_spec_ = GetServiceSpec(service_name, global_description);
  service_instance_ = service_instance;
  absl::Status ret = InitializeTables();
  if (!ret.ok()) return ret;

  // Start server
  std::string server_address = absl::StrCat("[::]:", *port);
  grpc::ServerBuilder builder;
  builder.SetMaxReceiveMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> server_creds =
    MakeServerCredentials();
  builder.AddListeningPort(server_address, server_creds, port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  server_address = absl::StrCat("[::]:", *port);  // port may have changed
  if (server_) {
    LOG(INFO) << "Engine server listening on " << server_address;
  } else {
    LOG(ERROR) << "Engine start failed on " << server_address;
    return absl::UnknownError("Engine service failed to start");
  }

  std::map<std::string, int> services =
    EnumerateServiceTypes(traffic_config_);
  auto it = services.find(service_name_);

  if (it == services.end()) {
    LOG(ERROR) << "could not find service to run: " << service_name_;
    return absl::NotFoundError("Service not found in config.");
  }

  service_index_ = it->second;
  return absl::OkStatus();
}

absl::Status DistBenchEngine::ConfigurePeers(
    const ServiceEndpointMap& peers) {
  pd_->SetHandler([this](ServerRpcState* state) { RpcHandler(state);});
  service_map_ = peers;
  if (service_map_.service_endpoints_size() < 2) {
    return absl::NotFoundError("No peers configured.");
  }

  return ConnectToPeers();
}

absl::Status DistBenchEngine::ConnectToPeers() {
  std::map<std::string, int> service_sizes =
    EnumerateServiceSizes(traffic_config_);
  std::map<std::string, int> service_instance_ids =
    EnumerateServiceInstanceIds(traffic_config_);
  std::map<std::string, int> service_index_map =
    EnumerateServiceTypes(traffic_config_);

  // peers_[service_id][instance_id]
  peers_.resize(traffic_config_.services_size());
  for (int i = 0; i < traffic_config_.services_size(); ++i) {
    peers_[i].resize(traffic_config_.services(i).count());
  }

  int num_targets = 0;
  std::string my_name = absl::StrCat(service_name_, "/", service_instance_);
  for (const auto& service : service_map_.service_endpoints()) {
    auto it = service_instance_ids.find(service.first);
    CHECK(it != service_instance_ids.end());
    int peer_trace_id = it->second;
    std::vector<std::string> service_and_instance =
      absl::StrSplit(service.first, '/');
    CHECK_EQ(service_and_instance.size(), 2ul);
    auto& service_type = service_and_instance[0];
    int instance;
    CHECK(absl::SimpleAtoi(service_and_instance[1], &instance));
    if (service.first == my_name) {
      trace_id_ = peer_trace_id;
    }
    auto it2 = service_index_map.find(service_type);
    CHECK(it2 != service_index_map.end());
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
        auto& rpc_state = pending_rpcs[rpc_count];
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
        rpc_state.rpc = rpc_state.stub->AsyncSetupConnection(
            &rpc_state.context, rpc_state.request, &cq);
        rpc_state.rpc->Finish(
            &rpc_state.response, &rpc_state.status, &rpc_state);
      }
    }
  }
  while (rpc_count) {
    bool ok;
    void* tag;
    cq.Next(&tag, &ok);
    if (ok) {
      --rpc_count;
      PendingRpc *finished_rpc = static_cast<PendingRpc*>(tag);
      if (!finished_rpc->status.ok()) {
        status = finished_rpc->status;
        LOG(ERROR) << "ConnectToPeers error:"
                   << finished_rpc->status.error_code()
                   << " "
                   << finished_rpc->status.error_message()
                   << " connecting to "
                   << finished_rpc->server_address;
      }
    }
  }
  for (size_t i = 0; i < pending_rpcs.size(); ++i) {
    absl::Status final_status = pd_->HandleConnect(
        pending_rpcs[i].response.responder_info(), i);
    if (!final_status.ok()) {
      LOG(INFO) << "weird, a connect failed after rpc succeeded.";
      status = abslStatusToGrpcStatus(final_status);
    }
  }
  return grpcStatusToAbslStatus(status);
}

absl::Status DistBenchEngine::RunTraffic(const RunTrafficRequest* request) {
  if (service_map_.service_endpoints_size() < 2) {
    return absl::NotFoundError("No peers configured.");
  }
  for (int i = 0; i < traffic_config_.action_lists_size(); ++i) {
    if (service_name_ == traffic_config_.action_lists(i).name()) {
      LOG(INFO) << "running Main for " << service_name_
                << "/" << service_instance_;
      engine_main_thread_ = RunRegisteredThread(
          "EngineMain", [this, i]() {RunActionList(i, nullptr);});
    }
  }
  return absl::OkStatus();
}

void DistBenchEngine::CancelTraffic() {
  LOG(INFO) << "did the cancelation now";
  canceled_.Notify();
}

ServicePerformanceLog DistBenchEngine::FinishTrafficAndGetLogs() {
  if (engine_main_thread_.joinable()) {
    engine_main_thread_.join();
    LOG(INFO) << "Finished running Main for "
              << service_name_ << "/" << service_instance_;
  }
  ServicePerformanceLog log;
  for (size_t i = 0; i < peers_.size(); ++i) {
    for (size_t j = 0; j < peers_[i].size(); ++j) {
      absl::MutexLock m(&peers_[i][j].mutex);
      if (!peers_[i][j].log.rpc_logs().empty()) {
        (*log.mutable_peer_logs())[peers_[i][j].log_name] =
          std::move(peers_[i][j].log);
      }
    }
  }
  return log;
}

void DistBenchEngine::RpcHandler(ServerRpcState* state) {
  // LOG(INFO) << state->request->ShortDebugString();
  CHECK(state->request->has_rpc_index());
  const auto& simulated_server_rpc =
      server_rpc_table_[state->request->rpc_index()];
  const auto& rpc_def = simulated_server_rpc.rpc_definition;

  // Perform action list
  int handler_action_index = simulated_server_rpc.handler_action_list_index;
  if (handler_action_index == -1) {
  } else {
    RunActionList(handler_action_index, state);
  }

  state->response.set_payload(std::string(rpc_def.response_payload_size, 'D'));

  // Send the response
  state->send_response();
}

void DistBenchEngine::RunActionList(
    int list_index, const ServerRpcState* incoming_rpc_state) {
  CHECK_LT(static_cast<size_t>(list_index), action_lists_.size());
  CHECK_GE(list_index, 0);
  // Optimize the "NOP" case:
  if (action_lists_[list_index].proto.action_names().empty()) {
    return;
  }
  ActionListState s;
  s.incoming_rpc_state = incoming_rpc_state;
  s.action_list = &action_lists_[list_index];

  // Allocate peer_logs for performance gathering, if needed:
  if (s.action_list->has_rpcs) {
    absl::MutexLock m(&s.action_mu);
    s.peer_logs.resize(peers_.size());
    for (size_t i = 0; i < peers_.size(); ++i) {
      s.peer_logs[i].resize(peers_[i].size());
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
          StartOpenLoopIteration(&s.state_table[i]);
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
      if (!deps_ready) {
        continue;
      }
      s.state_table[i].started = true;
      s.state_table[i].action = &s.action_list->list_actions[i];
      s.state_table[i].all_done_callback = [&s, i]() {s.FinishAction(i);};
      s.state_table[i].s = &s;
      RunAction(&s.state_table[i]);
    }
    absl::Time next_iteration_time = absl::InfiniteFuture();
    bool done = true;
    for (int i = 0; i < size; ++i) {
      if (!s.state_table[i].finished) {
        if (s.state_table[i].next_iteration_time < next_iteration_time) {
          next_iteration_time = s.state_table[i].next_iteration_time;
        }
        done = false;
        break;
      }
    }
    if (done) {
      break;
    }
    auto some_actions_finished = [&s]() {
      return !s.finished_action_indices.empty();
    };

    if (clock_->MutexLockWhenWithDeadline(
          &s.action_mu,
          absl::Condition(&some_actions_finished), next_iteration_time)) {
      if (s.finished_action_indices.empty()) {
        LOG(FATAL) << "finished_action_indices is empty";
      }
      for (const auto& finished_action_index : s.finished_action_indices) {
        s.state_table[finished_action_index].finished = true;
        s.state_table[finished_action_index].next_iteration_time =
          absl::InfiniteFuture();
      }
      s.finished_action_indices.clear();
    }
    s.action_mu.Unlock();
    if (canceled_.HasBeenNotified()) {
      LOG(INFO) << "cancelled an action list";
      s.WaitForAllPendingActions();
      break;
    }
  }
  // Merge the per-action-list logs into the overall logs:
  if (s.action_list->has_rpcs) {
    absl::MutexLock m(&s.action_mu);
    for (size_t i = 0; i < s.peer_logs.size(); ++i) {
      for (size_t j = 0; j < s.peer_logs[i].size(); ++j) {
        absl::MutexLock m(&peers_[i][j].mutex);
        for (const auto& rpc_log : s.peer_logs[i][j].rpc_logs()) {
          (*peers_[i][j].log.mutable_rpc_logs())[rpc_log.first].MergeFrom(
              rpc_log.second);
        }
      }
    }
  }
}

void DistBenchEngine::ActionListState::FinishAction(int action_index) {
  action_mu.Lock();
  finished_action_indices.push_back(action_index);
  action_mu.Unlock();
}

void DistBenchEngine::ActionListState::WaitForAllPendingActions() {
  auto some_actions_finished = [&]() {return !finished_action_indices.empty();};
  bool done;
  do {
    action_mu.LockWhen(absl::Condition(&some_actions_finished));
    finished_action_indices.clear();
    done = true;
    int size = action_list->proto.action_names_size();
    for (int i = 0; i < size; ++i) {
      const auto& state = state_table[i];
      if (state.started && !state.finished) {
        done = false;
        break;
      }
    }
    action_mu.Unlock();
  } while (!done);
}

void DistBenchEngine::ActionListState::RecordLatency(
    size_t rpc_index,
    size_t service_type,
    size_t instance,
    const ClientRpcState* state) {
  absl::MutexLock m(&action_mu);
  CHECK_LT(service_type, peer_logs.size());
  auto& service_log = peer_logs.at(service_type);
  CHECK_LT(instance, service_log.size());
  auto& peer_log = service_log.at(instance);
  auto& rpc_log = (*peer_log.mutable_rpc_logs())[rpc_index];
  auto* sample = state->success ? rpc_log.add_successful_rpc_samples()
                                : rpc_log.add_failed_rpc_samples();
  auto latency = state->end_time - state->start_time;
  sample->set_start_timestamp_ns(absl::ToUnixNanos(state->start_time));
  sample->set_latency_ns(absl::ToInt64Nanoseconds(latency));
  if (state->prior_start_time  != absl::InfinitePast()) {
    sample->set_latency_weight(absl::ToInt64Nanoseconds(
          state->start_time - state->prior_start_time));
  }
  sample->set_request_size(state->request.payload().size());
  sample->set_response_size(state->response.payload().size());
  if (!state->request.trace_context().engine_ids().empty()) {
    *sample->mutable_trace_context() = state->request.trace_context();
  }
}

void DistBenchEngine::RunAction(ActionState* action_state) {
  auto& action = *action_state->action;
  if (action.actionlist_index >= 0) {
    int action_list_index = action.actionlist_index;
    action_state->iteration_function =
      [this, action_list_index]
      (std::shared_ptr<ActionIterationState>iteration_state) {
      RunRegisteredThread(
          "ActionListThread",
          [this, action_list_index, iteration_state]() mutable {
            RunActionList(action_list_index, nullptr);
            FinishIteration(iteration_state);
          }).detach();
      };
  } else if (action.rpc_service_index >= 0) {
    CHECK_LT(static_cast<size_t>(action.rpc_service_index), peers_.size());
    std::shared_ptr<ServerRpcState> server_rpc_state =
      std::make_shared<ServerRpcState>();
    int rpc_service_index = action.rpc_service_index;
    CHECK_GE(rpc_service_index, 0);
    CHECK_LT(static_cast<size_t>(rpc_service_index), peers_.size());

    if (peers_[rpc_service_index].empty()) {
      return;
    }
    action_state->rpc_index = action.rpc_index;
    action_state->rpc_service_index = rpc_service_index;
    action_state->iteration_function =
      [this] (std::shared_ptr<ActionIterationState> iteration_state) {
        RunRpcActionIteration(iteration_state);
      };
  } else {
    LOG(FATAL) << "Do not support simulated computations yet";
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
      time_limit = clock_->Now() + absl::Microseconds(
          action.proto.iterations().max_duration_us());
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
  action_state->iteration_mutex.Unlock();

  if (open_loop) {
    CHECK_EQ(action_state->next_iteration_time, absl::InfiniteFuture());
    absl::Duration period = absl::Nanoseconds(
        action_state->action->proto.iterations().open_loop_interval_ns());
    if (action_state->action->proto.iterations()
        .open_loop_interval_distribution() == "sync_burst") {
      absl::Duration start = clock_->Now() - absl::UnixEpoch();
      action_state->next_iteration_time =
        period + absl::UnixEpoch() + absl::Floor(start, period);
    } else {
      action_state->next_iteration_time = clock_->Now();
      StartOpenLoopIteration(action_state);
    }
  } else {
    int64_t parallel_copies = std::min(
        action.proto.iterations().max_parallel_iterations(), max_iterations);
    for (int i = 0; i < parallel_copies; ++i) {
      auto it_state = std::make_shared<ActionIterationState>();
      it_state->action_state = action_state;
      action_state->iteration_mutex.Lock();
      it_state->iteration_number = ++action_state->next_iteration;
      action_state->iteration_mutex.Unlock();
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
  action_state->next_iteration_time += period;
  it_state->iteration_number = ++action_state->next_iteration;
  action_state->iteration_mutex.Unlock();
  StartIteration(it_state);
}

void DistBenchEngine::FinishIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  ActionState* state = iteration_state->action_state;
  bool open_loop = state->action->proto.iterations().has_open_loop_interval_ns();
  bool start_another_iteration = !open_loop;
  bool done = false;
  state->iteration_mutex.Lock();
  ++state->finished_iterations;
  if (state->next_iteration == state->iteration_limit) {
    done = true;
    start_another_iteration = false;
  } else if (state->next_iteration_time == absl::InfiniteFuture()) {
    // Closed loop iteration:
    if (state->time_limit != absl::InfiniteFuture()) {
      if (clock_->Now() > state->time_limit) {
        done = true;
        start_another_iteration = false;
      }
    }
  } else {
    // Open loop (possibly sync_burst) iteration:
    if (state->next_iteration_time > state->time_limit) {
      done = true;
      start_another_iteration = false;
    }
  }
  if (start_another_iteration) {
    ++state->next_iteration;
  }
  int pending_iterations = state->next_iteration - state->finished_iterations;
  state->iteration_mutex.Unlock();
  if (done && !pending_iterations) {
    state->all_done_callback();
  } else if (start_another_iteration) {
    StartIteration(iteration_state);
  }
}

void DistBenchEngine::StartIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  iteration_state->action_state->iteration_function(iteration_state);
}

// This works fine for 1-at-a-time closed-loop iterations:
void DistBenchEngine::RunRpcActionIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  ActionState* state = iteration_state->action_state;
  // Pick the subset of the target service instances to fanout to:
  std::vector<int> current_targets = PickRpcFanoutTargets(state);
  iteration_state->rpc_states.resize(current_targets.size());
  iteration_state->remaining_rpcs = current_targets.size();

  // Setup tracing:
  const auto& rpc_def = client_rpc_table_[state->rpc_index].rpc_definition;
  const auto& rpc_spec = rpc_def.rpc_spec;
  bool do_trace = false;
  int trace_count = ++client_rpc_table_[state->rpc_index].rpc_tracing_counter;
  if (rpc_spec.tracing_interval() > 0) {
    do_trace = (trace_count % rpc_spec.tracing_interval()) == 0;
  }
  GenericRequest common_request;
  if (state->s->incoming_rpc_state) {
    *common_request.mutable_trace_context() =
      state->s->incoming_rpc_state->request->trace_context();
  } else if (do_trace) {
    common_request.mutable_trace_context()->add_engine_ids(trace_id_);
    common_request.mutable_trace_context()->add_iterations(
        iteration_state->iteration_number);
  }
  common_request.set_rpc_index(state->rpc_index);

  common_request.set_payload(std::string(rpc_def.request_payload_size, 'D'));

  const auto& servers = peers_[state->rpc_service_index];
  for (size_t i = 0; i < current_targets.size(); ++i) {
    int peer_instance = current_targets[i];
    absl::MutexLock m(&peers_[state->rpc_service_index][peer_instance].mutex);
    ClientRpcState* rpc_state = &iteration_state->rpc_states[i];
    rpc_state->request = common_request;
    if (!common_request.trace_context().engine_ids().empty()) {
      rpc_state->request.mutable_trace_context()->add_engine_ids(
          peers_[state->rpc_service_index][peer_instance].trace_id);
      rpc_state->request.mutable_trace_context()->add_iterations(i);
    }
    CHECK_EQ(rpc_state->request.trace_context().engine_ids().size(),
             rpc_state->request.trace_context().iterations().size());
    rpc_state->prior_start_time = rpc_state->start_time;
    rpc_state->start_time = clock_->Now();
    pd_->InitiateRpc(servers[peer_instance].pd_id, rpc_state,
        [this, rpc_state, iteration_state, peer_instance]() mutable {
        ActionState* state = iteration_state->action_state;
        rpc_state->end_time = clock_->Now();
        state->s->RecordLatency(
            state->rpc_index,
            state->rpc_service_index,
            peer_instance,
            rpc_state);
        if (--iteration_state->remaining_rpcs == 0) {
          FinishIteration(iteration_state);
        }
      });
  }
}

// Return a vector of service instances, which have to be translated to
// protocol_drivers endpoint ids by the caller.
std::vector<int> DistBenchEngine::PickRpcFanoutTargets(ActionState* state) {
  const auto& rpc_def = client_rpc_table_[state->rpc_index].rpc_definition;
  const auto& rpc_spec = rpc_def.rpc_spec;
  std::vector<int> targets;
  int num_servers = peers_[state->rpc_service_index].size();
  const std::string& fanout_filter = rpc_spec.fanout_filter();

  if (rpc_def.is_stochastic_fanout) {
    std::map<int, std::vector<int>> partial_rand_vects =
        state->partially_randomized_vectors;

    int nb_targets = 0;
    float random_val = absl::Uniform(random_generator, 0, 1.0);
    float cur_val = 0.0;
    for (const auto &d : rpc_def.stochastic_dist) {
      cur_val += d.probability;
      if (random_val <= cur_val) {
        nb_targets = d.nb_targets;
        break;
      }
    }
    if (nb_targets > num_servers)
      nb_targets = num_servers;

// Potential optimization
//    if (nb_targets == 1) {
//      int target = random() % num_servers;
//      CHECK_NE(target, -1);
//      targets.push_back(target);
//      return targets;
//    }

    // Generate a vector to pick random targets from (only done once)
    partial_rand_vects.try_emplace(num_servers, std::vector<int>());
    std::vector<int>& from_vector = partial_rand_vects[num_servers];
    if (from_vector.empty()) {
      for (int i = 0; i < num_servers; i++)
        from_vector.push_back(i);
    }

    // Randomize and pick up to nb_targets
    for (int i = 0; i < nb_targets; i++) {
      int rnd_pos = i + (random() % (num_servers - i));
      std::swap(from_vector[i], from_vector[rnd_pos]);
      int target = from_vector[i];
      CHECK_NE(target, -1);
      targets.push_back(target);
    }
  } else if (fanout_filter == "all") {
    targets.reserve(num_servers);
    for (int i = 0; i < num_servers; ++i) {
      int target = i;
      if (state->rpc_service_index != service_index_ ||
          target != service_instance_) {
        CHECK_NE(target, -1);
        targets.push_back(target);
      }
    }
  } else {  // The following fanout options return 1 target
    int target;
    targets.reserve(1);
    if (fanout_filter == "random") {
      target = random() % num_servers;
    } else if (fanout_filter == "round_robin") {
      int64_t iteration =
          client_rpc_table_[state->rpc_index].rpc_tracing_counter;
      target = iteration % num_servers;
    } else {
      // Default case: return the first instance of the service
      target = 0;
    }
    CHECK_NE(target, -1);
    targets.push_back(target);
  }

  return targets;
}

}  // namespace distbench
