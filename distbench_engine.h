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

#ifndef DISTBENCH_DISTBENCH_ENGINE_H_
#define DISTBENCH_DISTBENCH_ENGINE_H_

#include <unordered_set>

#include "distbench.grpc.pb.h"
#include "distbench_utils.h"
#include "protocol_driver.h"
#include "absl/random/random.h"

namespace distbench {

class SimpleClock {
 public:
  virtual ~SimpleClock() = default;
  virtual absl::Time Now() = 0;
  virtual bool MutexLockWhenWithDeadline(
      absl::Mutex* mu, const absl::Condition& condition, absl::Time deadline)
    ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) = 0;
};

class RealClock : public SimpleClock {
 public:
  ~RealClock() override {}
  absl::Time Now() override { return absl::Now(); }

  bool MutexLockWhenWithDeadline(
      absl::Mutex* mu, const absl::Condition& condition, absl::Time deadline)
    ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) override {
    return mu->LockWhenWithDeadline(condition, deadline);
  }
};

class DistBenchEngine : public ConnectionSetup::Service {
 public:
  explicit DistBenchEngine(
      std::unique_ptr<ProtocolDriver> pd, SimpleClock* clock);
  ~DistBenchEngine() override;

  absl::Status Initialize(
      int port,
      const DistributedSystemDescription& global_description,
      std::string_view service_name,
      int service_instance);

  absl::Status ConfigurePeers(const ServiceEndpointMap& peers);
  absl::Status RunTraffic(const RunTrafficRequest* request);
  void CancelTraffic();

  ServicePerformanceLog FinishTrafficAndGetLogs();

  grpc::Status SetupConnection(grpc::ServerContext* context,
                               const ConnectRequest* request,
                               ConnectResponse* response) override;

 private:
  struct StochasticDist{
    float probability;
    int nb_targets;
  };

  struct RpcDefinition {
    // Original proto
    RpcSpec rpc_spec;

    // Used to store decoded stochastic fanout
    bool is_stochastic_fanout;
    std::vector<StochasticDist> stochastic_dist;

    // Decoded
    int request_payload_size;
    int response_payload_size;
  };

  struct PeerMetadata {
    PeerMetadata() {}
    PeerMetadata(const PeerMetadata& from) {
      from.mutex.Lock();
      log_name = from.log_name;
      log = from.log;
      trace_id = from.trace_id;
      from.mutex.Unlock();
    }

    std::string log_name;
    std::string endpoint_address;
    int trace_id;
    int pd_id = -1;
    PeerPerformanceLog log ABSL_GUARDED_BY(mutex);
    mutable absl::Mutex mutex;
  };

  struct SimulatedServerRpc {
    std::vector<GenericResponse> response_table;
    int handler_action_list_index = -1;
    RpcDefinition rpc_definition;
  };

  struct SimulatedClientRpc {
    int service_index;
    std::vector<GenericRequest> request_table;
    RpcDefinition rpc_definition;
    std::atomic<int64_t> rpc_tracing_counter = 0;
    std::vector<int> pending_requests_per_peer;
  };

  struct ActionTableEntry {
    Action proto;
    // index into peers_ that identifies the target(s) of this RPC
    int rpc_service_index = -1;
    // index into the client_rpc_table_/server_rpc_table_;
    int rpc_index = -1;
    int actionlist_index = -1;
    std::vector<int> dependent_action_indices;
  };

  struct ActionListTableEntry {
    ActionList proto;
    std::vector<ActionTableEntry> list_actions;
  };

  struct ActionListState;
  struct ActionState;

  struct ActionIterationState {
    struct ActionState* action_state = nullptr;
    int iteration_number = 0;
    std::vector<ClientRpcState> rpc_states;
    std::atomic<int> remaining_rpcs = 0;
  };

  struct ActionState {
    bool started = false;
    bool finished = false;
    ActionListState* s;

    absl::Mutex iteration_mutex;
    int next_iteration ABSL_GUARDED_BY(iteration_mutex);
    int finished_iterations ABSL_GUARDED_BY(iteration_mutex);
    absl::Time next_iteration_time = absl::InfiniteFuture();

    int64_t iteration_limit = std::numeric_limits<int64_t>::max();
    absl::Time time_limit = absl::InfiniteFuture();


    const ActionTableEntry* action = nullptr;
    int rpc_index;
    int rpc_service_index;

    std::function<void(std::shared_ptr<ActionIterationState> iteration_state)>
      iteration_function;
    std::function<void(void)> all_done_callback;

    std::map<int, std::vector<int>> partially_randomized_vectors;
  };

  struct ActionListState {
    void FinishAction(int action_index);
    void WaitForAllPendingActions();
    void RecordLatency(
        size_t rpc_index,
        size_t service_type,
        size_t instance,
        const ClientRpcState* state);

    const ServerRpcState* incoming_rpc_state = nullptr;  // may be nullptr
    std::unique_ptr<ActionState[]> state_table;
    const ActionListTableEntry* action_list;
    absl::Mutex action_mu;
    std::vector<int> finished_action_indices;

    std::vector<std::vector<PeerPerformanceLog>> peer_logs;
  };

  absl::Status InitializeTables();
  absl::Status InitializePayloadsMap();
  absl::Status InitializeRpcDefinitionStochastic(
      RpcDefinition& rpc_def);
  absl::Status InitializeRpcDefinitionsMap();

  void RunActionList(int list_index, const ServerRpcState* incoming_rpc_state);
  void RunAction(ActionState* action_state);
  void StartOpenLoopIteration(ActionState* action_state);
  void StartIteration(std::shared_ptr<ActionIterationState> iteration_state);
  void FinishIteration(std::shared_ptr<ActionIterationState> iteration_state);

  void RunRpcActionIteration(
      std::shared_ptr<ActionIterationState> iteration_state);
  std::vector<int> PickRpcFanoutTargets(ActionState* action_state);

  std::unique_ptr<SimulatedClientRpc[]> client_rpc_table_;
  std::vector<SimulatedServerRpc> server_rpc_table_;
  std::vector<ActionListTableEntry> action_lists_;

  absl::Status ConnectToPeers();
  void RpcHandler(ServerRpcState* state);

  int get_payload_size(const std::string& name);

  absl::Notification canceled_;
  DistributedSystemDescription traffic_config_;
  ServiceEndpointMap service_map_;
  std::string service_name_;
  ServiceSpec service_spec_;
  std::set<std::string> dependent_services_;
  int service_index_;
  int service_instance_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<ProtocolDriver> pd_;
  std::thread engine_main_thread_;

  // Payloads definitions
  std::map<std::string, PayloadSpec> payload_map_;
  std::map<std::string, RpcDefinition> rpc_map_;

  // The first index is the service, the second is the instance.
  std::vector<std::vector<PeerMetadata>> peers_;
  int trace_id_ = -1;
  SimpleClock* clock_ = nullptr;

  // Random
  absl::BitGen random_generator;
};

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_ENGINE_H_
