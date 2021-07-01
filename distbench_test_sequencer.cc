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

#include "distbench_test_sequencer.h"

#include "distbench_utils.h"
#include "glog/logging.h"

namespace distbench {

grpc::Status TestSequencer::RegisterNode(grpc::ServerContext* context,
                                         const NodeRegistration* request,
                                         NodeConfig* response) {
  if (request->hostname().empty() ||
      request->control_port() <= 0) {
    return grpc::Status(
        grpc::StatusCode::INVALID_ARGUMENT, "Invalid Registration");
  }

  absl::MutexLock m(&mutex_);
  int node_id = registered_nodes_.size();
  std::string node_alias;
  std::string registration = request->DebugString();
  auto it = node_registration_id_map_.find(registration);
  if (it != node_registration_id_map_.end()) {
    node_id = it->second;
    node_alias = registered_nodes_[node_id].node_alias;
    LOG(INFO) << "got repeated registration for node" << node_id;
  } else {
    registered_nodes_.emplace_back();
    node_alias = absl::StrCat("node", node_id);
    registered_nodes_.back().node_alias = node_alias;
    node_registration_id_map_[registration] = node_id;
    node_alias_id_map_[node_alias] = node_id;
  }

  std::shared_ptr<grpc::ChannelCredentials> creds =
    MakeChannelCredentials();
  std::string node_service =
    absl::StrCat("dns:///", request->hostname(), ":", request->control_port());
  std::shared_ptr<grpc::Channel> channel =
    grpc::CreateCustomChannel(node_service, creds,
                              GetDefaultChannelArguments());
  auto stub = DistBenchNodeManager::NewStub(channel);
  if (stub) {
    response->set_node_id(node_id);
    response->set_node_alias(node_alias);
    auto& node = registered_nodes_[node_id];
    node.registration = *request;
    node.stub = std::move(stub);
    LOG(INFO) << "Connected to " << response->node_alias()
              << " @ " << node_service;
    return grpc::Status::OK;
  } else {
    return grpc::Status(
        grpc::StatusCode::UNKNOWN, "Could not create node stub.");
  }
}

grpc::Status TestSequencer::RunTestSequence(grpc::ServerContext* context,
                                            const TestSequence* request,
                                            TestSequenceResults* response) {
  std::shared_ptr<absl::Notification> prior_notification;
  CancelTraffic();
  mutex_.Lock();
  do {
    if (running_test_sequence_context_) {
      running_test_sequence_context_->TryCancel();
    }
    prior_notification = running_test_notification_;
    mutex_.Unlock();
    if (prior_notification) {
      prior_notification->WaitForNotification();
    }
    mutex_.Lock();
  } while (running_test_sequence_context_);

  running_test_sequence_context_ = context;
  auto notification = running_test_notification_ =
    std::make_shared<absl::Notification>();
  mutex_.Unlock();
  grpc::Status result = DoRunTestSequence(context, request, response);
  notification->Notify();
  mutex_.Lock();
  running_test_sequence_context_ = nullptr;
  mutex_.Unlock();
  return result;
}

void TestSequencer::CancelTraffic() {
  absl::ReaderMutexLock m(&mutex_);
  grpc::CompletionQueue cq;
  struct PendingRpc {
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientAsyncResponseReader<CancelTrafficResult>> rpc;
    grpc::Status status;
    CancelTrafficRequest request;
    CancelTrafficResult response;
    RegisteredNode* node;
  };
  std::vector<PendingRpc> pending_rpcs(registered_nodes_.size());
  int rpc_count = 0;
  for (auto& node_it : registered_nodes_) {
    if (node_it.idle) {
      LOG(INFO) << "node " << node_it.node_alias << " was already idle";
      continue;
    }
    LOG(INFO) << "node " << node_it.node_alias << " was busy";
    auto& rpc_state = pending_rpcs[rpc_count];
    ++rpc_count;
    rpc_state.node = &node_it;
    rpc_state.rpc = node_it.stub->AsyncCancelTraffic(
          &rpc_state.context, rpc_state.request, &cq);
    rpc_state.rpc->Finish(&rpc_state.response, &rpc_state.status, &rpc_state);
  }
  while (rpc_count) {
    bool ok;
    void* tag;
    cq.Next(&tag, &ok);
    if (ok) {
      --rpc_count;
      PendingRpc *finished_rpc = static_cast<PendingRpc*>(tag);
      if (!finished_rpc->status.ok()) {
        LOG(ERROR) << "cancelling traffic " << finished_rpc->status;
      }
      finished_rpc->node->idle = true;
    }
  }
}

grpc::Status TestSequencer::DoRunTestSequence(grpc::ServerContext* context,
                                              const TestSequence* request,
                                              TestSequenceResults* response) {
  for (const auto& test : request->tests()) {
    {
      absl::MutexLock m(&mutex_);
      if (running_test_sequence_context_->IsCancelled()) {
        return grpc::Status(
            grpc::StatusCode::ABORTED, "Cancelled by new test sequence.");
      }
    }
    auto maybe_result = DoRunTest(context, test);
    if (maybe_result.ok()) {
      auto summary = SummarizeTestResult(maybe_result.value());
      for (auto s: summary) {
        maybe_result->add_log_summary(s);
        LOG(INFO) << s;
      }
      *response->add_test_results() = maybe_result.value();
    } else {
      return grpc::Status(grpc::StatusCode::ABORTED,
                          std::string(maybe_result.status().message()));
    }
  }
  return grpc::Status::OK;
}

absl::StatusOr<std::map<std::string, std::set<std::string>>>
TestSequencer::PlaceServices(const DistributedSystemDescription& test) {
  absl::MutexLock m(&mutex_);
  std::vector<std::string> all_services;
  std::set<std::string> unplaced_services;
  std::set<std::string> idle_nodes;
  {
    for (const auto& node : registered_nodes_) {
      idle_nodes.insert(node.node_alias);
    }
  }

  int total_services = 0;
  for (const auto& service_node : test.services()) {
    for (int i = 0; i < service_node.count(); ++i) {
      ++total_services;
    }
  }
  all_services.reserve(total_services);
  for (const auto& service_node : test.services()) {
    for (int i = 0; i < service_node.count(); ++i) {
      std::string service_instance =
        absl::StrCat(service_node.name(), "/", i);
      unplaced_services.insert(service_instance);
      all_services.push_back(service_instance);
    }
  }
  std::map<std::string, std::set<std::string>> node_service_map;
  for (const auto& service_bundle : test.node_service_bundles()) {
    for (const auto& service : service_bundle.second.services()) {
      auto it = unplaced_services.find(service);
      if (it == unplaced_services.end()) {
        return absl::NotFoundError(absl::StrCat(
              "Service ", service, " was not found or already placed."));
      } else {
        node_service_map[service_bundle.first].insert(service);
        unplaced_services.erase(it);
      }
    }
    auto it = idle_nodes.find(service_bundle.first);
    if (it == idle_nodes.end()) {
      return absl::NotFoundError(absl::StrCat(
            "Node ", service_bundle.first, " was not found or not idle."));
    } else {
      idle_nodes.erase(it);
    }
  }

  if (unplaced_services.empty()) {
    LOG(INFO) << "All services placed manually";
  } else {
    LOG(INFO) << "After manually assigned services "
              << unplaced_services.size() << " still need to be placed";

    std::vector<std::string> remaining_services;
    for (const auto& service : all_services) {
      auto it = unplaced_services.find(service);
      if (it != unplaced_services.end()) {
        remaining_services.push_back(service);
      }
    }
    std::vector<std::string> remaining_nodes;
    for (const auto& node : registered_nodes_) {
      auto it = idle_nodes.find(node.node_alias);
      if (it != idle_nodes.end()) {
        remaining_nodes.push_back(node.node_alias);
      }
    }
    std::string failures;
    for (size_t i = 0; i < remaining_services.size(); ++i) {
      if (i >= remaining_nodes.size()) {
        LOG(INFO) << "couldn't place service " << remaining_services[i];
        if (!failures.empty()) {
          absl::StrAppend(&failures, ", ");
        }
        absl::StrAppend(&failures, remaining_services[i]);
      } else {
        node_service_map[remaining_nodes[i]].insert(remaining_services[i]);
        LOG(INFO) << "Placed service '" << remaining_services[i]
                  << "' on node " << remaining_nodes[i];
      }
    }

    if (!failures.empty()) {
      return absl::NotFoundError(absl::StrCat(
            "No idle node for placement of services: ", failures));
    }
  }

  // Make sure there is an entry for every registered node:
  for (const auto& node : registered_nodes_) {
    node_service_map[node.node_alias];
  }

  LOG(INFO) << "Service Placement:";
  for (const auto& node : node_service_map) {
    LOG(INFO) << node.first << ":";
    for (const auto& service : node.second) {
      LOG(INFO) << "  " << service;
    }
  }
  return node_service_map;
}

absl::StatusOr<TestResult> TestSequencer::DoRunTest(
    grpc::ServerContext* context,
    const DistributedSystemDescription& test) {
  if (test.services().empty()) {
    return absl::InvalidArgumentError("No services defined.");
  }

  auto maybe_map = PlaceServices(test);
  if (!maybe_map.ok()) {
    return maybe_map.status();
  }
  std::map<std::string, std::set<std::string>> node_service_map =
    maybe_map.value();

  ServiceEndpointMap service_map;
  auto cret = ConfigureNodes(node_service_map, test);
  if (cret.ok())
    service_map = *cret;
  else
    return cret.status();

  auto ipret = IntroducePeers(node_service_map, service_map);
  if (!ipret.ok())
    return ipret;
  auto maybe_logs = RunTraffic(node_service_map);
  if (maybe_logs.ok()) {
    TestResult ret;
    *ret.mutable_traffic_config() = test;
    *ret.mutable_placement() = service_map;
    *ret.mutable_service_logs() = maybe_logs.value();
    return ret;
  } else {
    return maybe_logs.status();
  }
}

absl::StatusOr<ServiceEndpointMap> TestSequencer::ConfigureNodes(
      const std::map<std::string, std::set<std::string>>& node_service_map,
      const DistributedSystemDescription& test) {
  absl::MutexLock m(&mutex_);
  grpc::CompletionQueue cq;
  struct PendingRpc {
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ServiceEndpointMap>> rpc;
    grpc::Status status;
    NodeServiceConfig request;
    ServiceEndpointMap response;
  };
  grpc::Status status;
  ServiceEndpointMap ret;
  std::vector<PendingRpc> pending_rpcs(node_service_map.size());
  int rpc_count = 0;
  for (const auto& node_services : node_service_map) {
    auto& rpc_state = pending_rpcs[rpc_count];
    ++rpc_count;
    *rpc_state.request.mutable_traffic_config() = test;
    for (const auto& service : node_services.second) {
      rpc_state.request.add_services(service);
    }
    auto it = node_alias_id_map_.find(node_services.first);
    CHECK(it != node_alias_id_map_.end())
      << "couldn't find " << node_services.first;
    rpc_state.rpc = registered_nodes_[it->second].stub->AsyncConfigureNode(
          &rpc_state.context, rpc_state.request, &cq);
    rpc_state.rpc->Finish(&rpc_state.response, &rpc_state.status, &rpc_state);
  }
  while (rpc_count) {
    bool ok;
    void* tag;
    cq.Next(&tag, &ok);
    if (ok) {
      --rpc_count;
      PendingRpc *finished_rpc = static_cast<PendingRpc*>(tag);
      LOG(INFO) << "Finished RPC status:" <<
                grpcStatusToAbslStatus(finished_rpc->status);
      if (!finished_rpc->status.ok()) {
        status = finished_rpc->status;
      }
      ret.MergeFrom(finished_rpc->response);
    }
  }
  if (status.ok()) {
    return ret;
  } else {
    return grpcStatusToAbslStatus(status);
  }
}

absl::Status TestSequencer::IntroducePeers(
    const std::map<std::string, std::set<std::string>>& node_service_map,
    ServiceEndpointMap service_map) {
  LOG(INFO) << "Broadcasting service map:\n" << service_map.DebugString();
  absl::MutexLock m(&mutex_);
  grpc::CompletionQueue cq;
  struct PendingRpc {
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientAsyncResponseReader<IntroducePeersResult>> rpc;
    grpc::Status status;
    ServiceEndpointMap request;
    IntroducePeersResult response;
  };
  grpc::Status status;
  std::vector<PendingRpc> pending_rpcs(node_service_map.size());
  int rpc_count = 0;
  for (const auto& node_services : node_service_map) {
    auto& rpc_state = pending_rpcs[rpc_count];
    ++rpc_count;
    rpc_state.request = service_map;
    auto it = node_alias_id_map_.find(node_services.first);
    CHECK(it != node_alias_id_map_.end());
    rpc_state.rpc = registered_nodes_[it->second].stub->AsyncIntroducePeers(
          &rpc_state.context, rpc_state.request, &cq);
    rpc_state.rpc->Finish(&rpc_state.response, &rpc_state.status, &rpc_state);
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
      }
    }
  }

  return grpcStatusToAbslStatus(status);
}

absl::StatusOr<ServiceLogs> TestSequencer::RunTraffic(
    const std::map<std::string, std::set<std::string>>& node_service_map) {
  absl::ReaderMutexLock m(&mutex_);
  grpc::CompletionQueue cq;
  struct PendingRpc {
    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ServiceLogs>> rpc;
    grpc::Status status;
    RunTrafficRequest request;
    ServiceLogs response;
    RegisteredNode* node;
  };
  grpc::Status status;
  ServiceLogs ret;
  std::vector<PendingRpc> pending_rpcs(node_service_map.size());
  int rpc_count = 0;
  for (const auto& node_services : node_service_map) {
    auto& rpc_state = pending_rpcs[rpc_count];
    ++rpc_count;
    auto it = node_alias_id_map_.find(node_services.first);
    CHECK(it != node_alias_id_map_.end());
    rpc_state.node = &registered_nodes_[it->second];
    rpc_state.node->idle = false;
    rpc_state.rpc = rpc_state.node->stub->AsyncRunTraffic(
          &rpc_state.context, rpc_state.request, &cq);
    rpc_state.rpc->Finish(&rpc_state.response, &rpc_state.status, &rpc_state);
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
      }
      ret.MergeFrom(finished_rpc->response);
      finished_rpc->node->idle = true;
    }
  }
  if (status.ok()) {
    return ret;
  } else {
    return grpcStatusToAbslStatus(status);
  }
}

void TestSequencer::Shutdown() {
  if (grpc_server_) {
    grpc_server_->Shutdown();
  }
}

void TestSequencer::Wait() {
  if (grpc_server_) {
    grpc_server_->Wait();
  }
}

TestSequencer::~TestSequencer() {
  if (grpc_server_) {
    grpc_server_->Shutdown();
    grpc_server_->Wait();
  }
}

void TestSequencer::Initialize(const TestSequencerOpts& opts) {
  opts_ = opts;
  service_address_ = absl::StrCat("[::]:", *opts_.port);
  grpc::ServerBuilder builder;
  builder.SetMaxMessageSize(std::numeric_limits<int32_t>::max());
  std::shared_ptr<grpc::ServerCredentials> creds = MakeServerCredentials();
  builder.AddListeningPort(service_address_, creds, opts_.port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(this);
  grpc_server_ = builder.BuildAndStart();
  service_address_ = absl::StrCat("[::]:", *opts_.port);  // port may have changed
  LOG(INFO) << "Server listening on " << service_address_;
}

}  // namespace distbench
