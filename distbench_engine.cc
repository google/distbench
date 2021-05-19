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
#include "absl/strings/str_split.h"
#include "include/grpcpp/create_channel.h"
#include "include/grpcpp/security/credentials.h"
#include <glog/logging.h>
#include "base/logging.h"

namespace distbench {

grpc::Status abslStatusToGrpcStatus(const absl::Status &status){
  if (status.ok())
    return grpc::Status::OK;

  std::string message = std::string(status.message());
  // GRPC and ABSL (currently) share the same error codes
  grpc::StatusCode code = (grpc::StatusCode)status.code();
  return grpc::Status(code, message);
}

grpc::Status abslStatusOrToGrpcStatus(const absl::StatusOr<std::string> &status_or){
  // This discard the ok value
  return abslStatusToGrpcStatus(status_or.status());
}

absl::Status grpcStatusToAbslStatus(const grpc::Status &status){
  if (status.ok())
    return absl::OkStatus();

  std::string message = status.error_message();
  // GRPC and ABSL (currently) share the same error codes
  absl::StatusCode code = (absl::StatusCode)status.error_code();
  return absl::Status(code, message);
}

grpc::Status DistBenchEngine::SetupConnection(grpc::ServerContext* context,
                                              const ConnectRequest* request,
                                              ConnectResponse* response) {
  auto maybe_info = pd_->HandlePreConnect(request->initiator_info(), 0);
  if (maybe_info.ok()) {
    response->set_responder_info(maybe_info.value());
    return grpc::Status::OK;
  } else {
    return abslStatusOrToGrpcStatus(maybe_info);
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
}

absl::Status DistBenchEngine::InitializeTables() {
  // Convert the action table to a map indexed by name:
  std::map<std::string, Action> action_map;
  for (int i = 0; i < traffic_config_.action_table_size(); ++i) {
    const auto& action = traffic_config_.action_table(i);
    action_map[action.name()] = traffic_config_.action_table(i);
  }
  std::map<std::string, int> rpc_name_index_map =
    EnumerateRpcs(traffic_config_);
  std::map<std::string, int> service_index_map =
    EnumerateServiceTypes(traffic_config_);

  std::map<std::string, int> action_list_index_map;
  action_list_table_.resize(traffic_config_.action_list_table().size());
  for (int i = 0; i < traffic_config_.action_list_table_size(); ++i) {
    const auto& action_list = traffic_config_.action_list_table(i);
    action_list_index_map[action_list.name()] = i;
    action_list_table_[i].proto = action_list;
    action_list_table_[i].list_actions.resize(action_list.action_names_size());
  }

  for (int i = 0; i < traffic_config_.action_list_table_size(); ++i) {
    const auto& action_list = traffic_config_.action_list_table(i);
    std::map<std::string, int> list_action_indices;
    for (int j = 0; j < action_list.action_names().size(); ++j) {
      const auto& action_name = action_list.action_names(j);
      list_action_indices[action_name] = j;
      auto it = action_map.find(action_name);
      if (it == action_map.end()) {
        return absl::NotFoundError(action_name);
      }
      if (it->second.has_rpc_name()) {
        // Validate rpc can be sent from this local node
      }
      action_list_table_[i].list_actions[j].proto = it->second;
    }
    // second pass to fixup deps:
    for (size_t j = 0; j < action_list_table_[i].list_actions.size(); ++j) {
      auto& action = action_list_table_[i].list_actions[j];
      if (action.proto.has_rpc_name()) {
        auto it2 = rpc_name_index_map.find(action.proto.rpc_name());
        if (it2 == rpc_name_index_map.end()) {
          return absl::NotFoundError(action.proto.rpc_name());
        }
        action.rpc_index = it2->second;
        LOG(INFO) << "action " << action.proto.name()
                  << " has rpc index " << it2->second;
        std::string target_service_name = GetServerServiceName(
            traffic_config_.rpc_descriptions(action.rpc_index));
        auto it3 = service_index_map.find(target_service_name);
        if (it3 == service_index_map.end()) {
          return absl::NotFoundError(target_service_name);
        }
        action.rpc_service_index = it3->second;
        LOG(INFO) << "action " << action.proto.name()
                  << " has rpc service index " << it3->second;
      } else if (action.proto.has_action_list_name()) {
        auto it4 = action_list_index_map.find(action.proto.action_list_name());
        if (it4 == action_list_index_map.end()) {
          return absl::InvalidArgumentError(absl::StrCat(
                "Action_list not found: ", action.proto.action_list_name()));
        }
        action.actionlist_index = it4->second;
      } else {
        LOG(QFATAL) << "only rpc actions are supported for now";
      }
      action.dependent_action_indices.resize(action.proto.dependencies_size());
      for (int k = 0; k < action.proto.dependencies_size(); ++k) {
        auto it = list_action_indices.find(action.proto.dependencies(k));
        if (it == list_action_indices.end()) {
          return absl::NotFoundError(action.proto.dependencies(k));
        }
        action.dependent_action_indices[k] = it->second;
        if ((size_t)it->second >= j) {
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
    const std::string server_service_name = GetServerServiceName(rpc);
    const std::string client_service_name = GetClientServiceName(rpc);
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
      LOG(INFO) << "local service " << service_name_
                << " uses service " << server_service_name;
    }
    if (server_service_name == service_name_) {
      server_rpc_set.insert(rpc.name());
    }

    auto it = action_list_index_map.find(rpc.handler_action_list_name());
    if (it == action_list_index_map.end()) {
      return absl::NotFoundError(rpc.handler_action_list_name());
    }
    server_rpc_table_[i].handler_action_list_index = it->second;

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
    client_rpc_table_[i].server_type_index = it1->second;
    client_rpc_table_[i].rpc_spec = rpc;
    client_rpc_table_[i].pending_requests_per_peer.resize(
        traffic_config_.services(it1->second).count(), 0);
  }

  return absl::OkStatus();
}

absl::Status DistBenchEngine::Initialize(
    int port,
    const DistributedSystemDescription& global_description,
    std::string_view service_name,
    int service_instance) {
  traffic_config_ = global_description;
  QCHECK(!service_name.empty());
  service_name_ = service_name;
  service_spec_ = GetServiceSpec(service_name, global_description);
  LOG(INFO) << service_spec_.DebugString();
  service_instance_ = service_instance;
  absl::Status ret = InitializeTables();
  if (!ret.ok()) return ret;
  std::string server_address = absl::StrCat("[::]:", port);
  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> server_creds =
    MakeServerCredentials();
  builder.AddListeningPort(server_address, server_creds);
  builder.RegisterService(this);
  LOG(INFO) << "Engine starting on " << server_address;
  server_ = builder.BuildAndStart();
  if (server_) {
    LOG(INFO) << "Engine server listening on " << server_address;
    return absl::OkStatus();
  } else {
    return absl::UnknownError("Engine service failed to start");
  }
}

absl::Status DistBenchEngine::ConfigurePeers(
    const ServiceEndpointMap& peers) {
  pd_->SetHandler([this](ServerRpcState* state) { RpcHandler(state);});
  service_map_ = peers;
  if (service_map_.service_endpoints_size() < 2) {
    return absl::NotFoundError("No peers configured.");
  }

  std::map<std::string, int> services =
    EnumerateServiceTypes(traffic_config_);
  auto it = services.find(service_name_);

  if (it == services.end()) {
    LOG(ERROR) << "could not find service to run: " << service_name_;
    return absl::NotFoundError("Service not found in config.");
  }

  absl::Status ret = ConnectToPeers();
  return ret;
}

absl::Status DistBenchEngine::ConnectToPeers() {
  std::map<std::string, int> service_sizes =
    EnumerateServiceSizes(traffic_config_);
  std::map<std::string, int> service_instance_ids =
    EnumerateServiceInstanceIds(traffic_config_);
  std::map<std::string, int> service_index_map =
    EnumerateServiceTypes(traffic_config_);
  peers_.resize(traffic_config_.services_size());
  for (int i = 0; i < traffic_config_.services_size(); ++i) {
    peers_[i].resize(traffic_config_.services(i).count());
  }
  int num_targets = 0;

  std::string my_name = absl::StrCat(service_name_, "/", service_instance_);
  for (const auto& service : service_map_.service_endpoints()) {
    auto it = service_instance_ids.find(service.first);
    QCHECK(it != service_instance_ids.end());
    int peer_trace_id = it->second;
    std::vector<std::string> service_and_instance =
      absl::StrSplit(service.first, '/');
    QCHECK_EQ(service_and_instance.size(), 2ul);
    auto& service_type = service_and_instance[0];
    int instance;
    QCHECK(absl::SimpleAtoi(service_and_instance[1], &instance));
    if (service.first == my_name) {
      trace_id_ = peer_trace_id;
    }
    auto it2 = service_index_map.find(service_type);
    QCHECK(it2 != service_index_map.end());
    LOG(INFO) << "peers_[" << it2->second << "][" << instance << "] = "
              << service.first << " / " << peer_trace_id;
    peers_[it2->second][instance].log_name = service.first;
    peers_[it2->second][instance].trace_id = peer_trace_id;

    if (dependent_services_.count(service_type)) {
      peers_[it2->second][instance].endpoint_address =
        service.second.endpoint_address();
      peers_[it2->second][instance].pd_id = num_targets;
      ++num_targets;
    }
  }
  LOG(INFO) << "service " << service_name_ << " has N dep peers: "
            << num_targets;
  pd_->SetNumPeers(num_targets);
  grpc::CompletionQueue cq;
  struct PendingRpc {
    std::unique_ptr<ConnectionSetup::Stub> stub;
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ConnectResponse>> rpc;
    grpc::Status status;
    ConnectRequest request;
    ConnectResponse response;
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
          grpc::CreateChannel(service_instance.endpoint_address, creds);
        rpc_state.stub = ConnectionSetup::NewStub(channel);
        QCHECK(rpc_state.stub);

        ++rpc_count;
        rpc_state.request.set_initiator_info(pd_->Preconnect().value()); //was ValueOrDie but value will throw an exception
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
        LOG(ERROR) << finished_rpc->status;
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
  for (int i = 0; i < traffic_config_.action_list_table_size(); ++i) {
    if (service_name_ == traffic_config_.action_list_table(i).name()) {
      LOG(INFO) << "running Main for " << service_name_
                << "/" << service_instance_;
      LOG(INFO) << "Main routine " << service_name_
                << " is action list index " << i;
      engine_main_thread_ = RunRegisteredThread(
          "EngineMain", [this, i](){RunActionList(i, nullptr);});
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
      if (!peers_[i][j].log.successful_rpc_samples().empty() ||
          !peers_[i][j].log.failed_rpc_samples().empty()) {
        (*log.mutable_peer_logs())[peers_[i][j].log_name] =
          std::move(peers_[i][j].log);
      }
    }
  }
  return log;
}

void DistBenchEngine::RpcHandler(ServerRpcState* state) {
  // LOG(INFO) << state->request->ShortDebugString();
  QCHECK(state->request->has_rpc_index());
  int handler_action_index =
    server_rpc_table_[state->request->rpc_index()].handler_action_list_index;
  if (handler_action_index == -1) {
  } else {
    LOG(INFO) << service_name_ << " rpc handler doing sth! rpc_index: "
              << state->request->rpc_index() << " list index: "
              << handler_action_index;
    RunActionList(handler_action_index, state);
  }
  state->send_response();
}

void DistBenchEngine::RunActionList(
    int list_index, const ServerRpcState* incoming_rpc_state) {
  QCHECK_LT((size_t)list_index, action_list_table_.size());
  QCHECK_GE(list_index, 0);
  ActionListState s;
  s.peer_logs.resize(peers_.size());
  for (size_t i = 0; i < peers_.size(); ++i) {
    s.peer_logs[i].resize(peers_[i].size());
  }
  s.incoming_rpc_state = incoming_rpc_state;
  s.action_list = &action_list_table_[list_index];
  int size = s.action_list->proto.action_names_size();
  s.finished_action_indices.reserve(size);
  s.state_table = std::make_unique<ActionState[]>(size);
  for (const auto& action : s.action_list->list_actions) {
    LOG(INFO) << action.proto.ShortDebugString();
    for (const auto& j : action.dependent_action_indices) {
      LOG(INFO) << "waits for " << j;
    }
  }
  while (true) {
    absl::Time now = clock_->Now();
    for (int i = 0; i < size; ++i) {
      if (s.state_table[i].started) {
        if (s.state_table[i].next_iteration_time < now) {
          LOG(INFO) << "open loopzzzzz";
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
      LOG(INFO) << service_name_ << " action ready to run: "
                << s.action_list->list_actions[i].proto.name();
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
    auto some_actions_finished = [&s](){
      return !s.finished_action_indices.empty();
    };

    if (clock_->MutexLockWhenWithDeadline(
          &s.action_mu,
          absl::Condition(&some_actions_finished), next_iteration_time)) {
      if (s.finished_action_indices.empty()) {
        LOG(QFATAL) << "finished_action_indices is empty";
      }
      for (const auto& finished_action_index : s.finished_action_indices) {
        s.state_table[finished_action_index].finished = true;
        s.state_table[finished_action_index].next_iteration_time =
          absl::InfiniteFuture();
      }
      s.finished_action_indices.clear();
    } else {
      LOG(INFO) << "deadline exceeded without some_actions_finished";
    }
    s.action_mu.Unlock();
    if (canceled_.HasBeenNotified()) {
      LOG(INFO) << "cancelled an action list";
      s.WaitForAllPendingActions();
      break;
    }
  }
  // Merge the per-thread logs into the overall logs:
  for (size_t i = 0; i < s.peer_logs.size(); ++i) {
    for (size_t j = 0; j < s.peer_logs[i].size(); ++j) {
      absl::MutexLock m(&peers_[i][j].mutex);
      peers_[i][j].log.MergeFrom(s.peer_logs[i][j]);
    }
  }
  LOG(INFO) << "finished running an action list: "
            << s.action_list->proto.name();
}

void DistBenchEngine::ActionListState::FinishAction(int action_index) {
  action_mu.Lock();
  finished_action_indices.push_back(action_index);
  action_mu.Unlock();
}

void DistBenchEngine::ActionListState::WaitForAllPendingActions() {
  auto some_actions_finished = [&](){return !finished_action_indices.empty();};
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
    int service_type, int instance, const ClientRpcState* state) {
  auto& log = peer_logs[service_type][instance];
  auto* sample = state->success ? log.add_successful_rpc_samples()
                                : log.add_failed_rpc_samples();
  auto latency = state->end_time - state->start_time;
  sample->set_start_timestamp_ns(absl::ToUnixNanos(state->start_time));
  sample->set_latency_ns(absl::ToInt64Nanoseconds(latency));
  if (state->prior_start_time  != absl::InfinitePast()) {
    sample->set_latency_weight(absl::ToInt64Nanoseconds(
          state->start_time - state->prior_start_time));
  }
  sample->set_request_size(state->request.payload().size());
  sample->set_response_size(state->response.payload().size());
  sample->set_rpc_index(state->request.rpc_index());
  if (!state->request.trace_context().engine_ids().empty()) {
    *sample->mutable_trace_context() = state->request.trace_context();
  }
}

void DistBenchEngine::RunAction(ActionState* action_state) {
  auto& action = *action_state->action;
  LOG(INFO) << "Executing action: " << action.proto.ShortDebugString();
  if (action.actionlist_index >= 0) {
    int action_list_index = action.actionlist_index;
    LOG(INFO) << service_name_ << " should be launching a new actionlist # "
              << action_list_index;
    action_state->iteration_function =
      [this, action_list_index]
      (std::shared_ptr<ActionIterationState>iteration_state) {
      RunRegisteredThread(
          "ActionListThread",
          [this, action_list_index, iteration_state]() mutable {
            RunActionList(action_list_index, nullptr);
            FinishIteration(iteration_state);
            LOG(INFO) << "done with popup thread";
          }).detach();
      };
  } else if (action.rpc_service_index >= 0) {
    LOG(INFO) << "ima rpc " << action.rpc_index
              << " to service index " << action.rpc_service_index
              << " out of " << peers_.size();
    CHECK_LT((size_t)action.rpc_service_index, peers_.size());
    int service_size = peers_[action.rpc_service_index].size();
    LOG(INFO) << "there are " << service_size << " replicas";
    std::shared_ptr<ServerRpcState> server_rpc_state =
      std::make_shared<ServerRpcState>();
    int rpc_service_index = action.rpc_service_index;
    QCHECK_GE(rpc_service_index, 0);
    QCHECK_LT((size_t)rpc_service_index, peers_.size());

    if (peers_[rpc_service_index].empty()) {
      LOG(INFO) << "empty service";
      return;
    }
    action_state->rpc_index = action.rpc_index;
    action_state->rpc_service_index = rpc_service_index;
    action_state->iteration_function =
      [this] (std::shared_ptr<ActionIterationState> iteration_state) {
        RunRpcActionIteration(iteration_state);
      };
  } else {
    LOG(QFATAL) << "Do not support computations yet";
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
      time_limit = absl::Now() + absl::Microseconds(
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
    StartOpenLoopIteration(action_state);
  } else {
    int64_t parallel_copies = std::min(
        action.proto.iterations().max_parallel_iterations(), max_iterations);
    for (int i = 0; i < parallel_copies; ++i) {
      auto it_state = std::make_shared<ActionIterationState>();
      it_state->action_state = action_state;
      StartIteration(it_state);
    }
  }
}

void DistBenchEngine::StartOpenLoopIteration(ActionState* action_state) {
  LOG(INFO) << "Starting open loop iteration";
  if (action_state->next_iteration_time == absl::InfiniteFuture()) {
    action_state->next_iteration_time = clock_->Now();
  }
  action_state->next_iteration_time += absl::Nanoseconds(
      action_state->action->proto.iterations().open_loop_interval_ns());
  auto it_state = std::make_shared<ActionIterationState>();
  it_state->action_state = action_state;
  StartIteration(it_state);
}

void DistBenchEngine::FinishIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  ActionState* state = iteration_state->action_state;
  bool done = false;
  state->iteration_mutex.Lock();
  ++state->finished_iterations;
  if (state->finished_iterations == state->iteration_limit) {
    done = true;
  }
  if (state->time_limit != absl::InfiniteFuture() &&
      state->time_limit < clock_->Now()) {
    done = true;
  }
  state->iteration_mutex.Unlock();
  if (done) {
    state->all_done_callback();
  } else if (!state->action->proto.iterations().has_open_loop_interval_ns()) {
    StartIteration(iteration_state);
  }
}

void DistBenchEngine::StartIteration(
    std::shared_ptr<ActionIterationState> iteration_state) {
  ActionState* state = iteration_state->action_state;
  state->iteration_mutex.Lock();
  LOG(INFO) << "!!!!!! " << state->next_iteration
            << " / " << state->iteration_limit << " " << iteration_state;
  if (state->next_iteration >= state->iteration_limit) {
    state->iteration_mutex.Unlock();
    return;
  }

  iteration_state->iteration_number = ++state->next_iteration;
  state->iteration_mutex.Unlock();
  state->iteration_function(iteration_state);
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
  const auto& rpc_spec = client_rpc_table_[state->rpc_index].rpc_spec;
  bool do_trace = false;
  int trace_count = ++client_rpc_table_[state->rpc_index].rpc_tracing_counter;
  if (rpc_spec.tracing_interval() > 0) {
    do_trace = !(trace_count % rpc_spec.tracing_interval());
    LOG(INFO) << trace_count << " % " << rpc_spec.tracing_interval();
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

  // Setup the request payload:
  common_request.set_payload(std::string(16, 'D'));

  for (size_t i = 0; i < current_targets.size(); ++i) {
    int peer = current_targets[i];
    absl::MutexLock m(&peers_[state->rpc_service_index][peer].mutex);
    ClientRpcState* rpc_state = &iteration_state->rpc_states[i];
    rpc_state->request = common_request;
    if (!common_request.trace_context().engine_ids().empty()) {
      rpc_state->request.mutable_trace_context()->add_engine_ids(
          peers_[state->rpc_service_index][peer].trace_id);
      rpc_state->request.mutable_trace_context()->add_iterations(i);
    }
    CHECK_EQ(rpc_state->request.trace_context().engine_ids().size(),
             rpc_state->request.trace_context().iterations().size());
    rpc_state->prior_start_time =
      rpc_state->start_time;
    rpc_state->start_time = clock_->Now();
    LOG(INFO) << service_name_ << " sending rpc to peer " << peer;
    pd_->InitiateRpc(peer, rpc_state,
        [this, rpc_state, iteration_state, peer]() mutable {
        LOG(INFO) << "finished rpc to peer " << peer;
        ActionState* state = iteration_state->action_state;
        rpc_state->end_time = clock_->Now();
        state->s->RecordLatency(
            state->rpc_service_index, peer, rpc_state);
        if (--iteration_state->remaining_rpcs == 0) {
          FinishIteration(iteration_state);
        }
      });
  }
}

std::vector<int> DistBenchEngine::PickRpcFanoutTargets(ActionState* state) {
  const auto& rpc_spec = client_rpc_table_[state->rpc_index].rpc_spec;
  std::vector<int> targets;
  int num_servers = peers_[state->rpc_service_index].size();
  auto& servers = peers_[state->rpc_service_index];
  LOG(INFO) << "num_s : " << num_servers;

  if (rpc_spec.fanout_filter() == "all") {
    targets.reserve(servers.size());
    for (int i = 0; i < num_servers; ++i) {
      int target = servers[i].pd_id;
      QCHECK_NE(target, -1);
      LOG(INFO) << "targeting " << target;
      targets.push_back(target);
    }
  } else if (rpc_spec.fanout_filter() == "random") {
    targets.reserve(1);
    int target = servers[random() % num_servers].pd_id;
    QCHECK_NE(target, -1);
    LOG(INFO) << "targeting " << target;
    targets.push_back(target);
  } else if (rpc_spec.fanout_filter() == "round_robin") {
    targets.reserve(1);
    int64_t iteration = client_rpc_table_[state->rpc_index].rpc_tracing_counter;
    int target = servers[iteration % num_servers].pd_id;
    QCHECK_NE(target, -1);
    LOG(INFO) << "targeting " << target;
    targets.push_back(target);
  } else {
    targets.reserve(1);
    int target = servers[0].pd_id;
    QCHECK_NE(target, -1);
    LOG(INFO) << "targeting " << target;
    targets.push_back(target);
  }
  return targets;
}

}  // namespace distbench
