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

#include "absl/random/random.h"
#include "activity.h"
#include "distbench.grpc.pb.h"
#include "distbench_utils.h"
#include "protocol_driver.h"
#include "randomization/distribution_sample_generator.h"

namespace distbench {

class DistBenchEngine : public ConnectionSetup::Service {
 public:
  explicit DistBenchEngine(std::unique_ptr<ProtocolDriver> pd);
  ~DistBenchEngine() override;

  absl::Status Initialize(
      const DistributedSystemDescription& global_description,
      std::string_view service_name, int service_instance, int* port);

  absl::Status ConfigurePeers(const ServiceEndpointMap& peers);
  absl::Status RunTraffic(const RunTrafficRequest* request);
  void CancelTraffic();

  void FinishTraffic();
  ServicePerformanceLog GetLogs();

  grpc::Status SetupConnection(grpc::ServerContext* context,
                               const ConnectRequest* request,
                               ConnectResponse* response) override;

 private:
  struct StochasticDist {
    float probability;
    int nb_targets;
  };

  enum FanoutFilter {
    kAll = 0,
    kRandomSingle = 1,
    kRoundRobin = 2,
    kStochastic = 3,
  };

  struct RpcDefinition {
    // Original proto
    RpcSpec rpc_spec;

    // Used to store decoded stochastic fanout
    FanoutFilter fanout_filter;
    std::vector<StochasticDist> stochastic_dist;

    // Decoded
    int request_payload_size;
    int response_payload_size;

    int sample_interpretor_index = -1;
  };

  struct PeerMetadata {
    PeerMetadata() {}
    PeerMetadata(const PeerMetadata& from) {
      from.mutex.Lock();
      log_name = from.log_name;
      partial_logs = from.partial_logs;
      trace_id = from.trace_id;
      from.mutex.Unlock();
    }

    std::string log_name;
    std::string endpoint_address;
    int trace_id;
    int pd_id = -1;
    std::list<PeerPerformanceLog> partial_logs ABSL_GUARDED_BY(mutex);
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
    int activity_config_index = -1;
    std::vector<int> dependent_action_indices;
  };

  struct ActionListTableEntry {
    ActionList proto;
    std::vector<ActionTableEntry> list_actions;
    bool has_rpcs = false;
  };

  struct ActionListState;
  struct ActionState;

  struct ActionIterationState {
    struct ActionState* action_state = nullptr;
    int iteration_number = 0;
    bool warmup = false;
    std::vector<ClientRpcState> rpc_states;
    std::atomic<int> remaining_rpcs = 0;
  };

  struct ActionState {
    bool started = false;
    bool finished = false;
    ActionListState* action_list_state;

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

    std::unique_ptr<Activity> activity;

    // Use rand_gen only from RunActionList's thread.
    std::default_random_engine* rand_gen = nullptr;
  };

  struct PackedLatencySample {
    bool operator<(const PackedLatencySample& other) const {
      return (start_timestamp_ns + latency_ns) <
             (other.start_timestamp_ns + other.latency_ns);
    }

    // Not using any in-class initializers so that these are trivially
    // destructible:
    size_t rpc_index;
    size_t service_type;
    size_t instance;
    bool success;
    bool warmup;
    int64_t request_size;
    int64_t response_size;
    int64_t start_timestamp_ns;
    int64_t latency_ns;
    int64_t latency_weight;
    size_t sample_number;
    TraceContext* trace_context;
  };

  static_assert(std::is_trivially_destructible<PackedLatencySample>::value);
  static_assert(std::is_trivially_constructible<PackedLatencySample>::value);

  struct ActionListState {
    void FinishAction(int action_index);
    void WaitForAllPendingActions();
    void CancelActivities();
    void UpdateActivitiesLog(
        std::map<std::string, std::map<std::string, int64_t>>*
            cumulative_activity_logs);
    bool DidSomeActionsFinish();
    void HandleFinishedActions();
    void RecordLatency(size_t rpc_index, size_t service_type, size_t instance,
                       ClientRpcState* state);
    void RecordPackedLatency(size_t sample_number, size_t index,
                             size_t rpc_index, size_t service_type,
                             size_t instance, ClientRpcState* state);
    void UnpackLatencySamples();

    const ServerRpcState* incoming_rpc_state = nullptr;
    std::unique_ptr<ActionState[]> state_table;
    const ActionListTableEntry* action_list;
    absl::Mutex action_mu;
    std::vector<int> finished_action_indices;

    std::vector<std::vector<PeerPerformanceLog>> peer_logs_
        ABSL_GUARDED_BY(action_mu);
    std::unique_ptr<PackedLatencySample[]> packed_samples_;
    size_t packed_samples_size_ = 0;
    std::atomic<size_t> packed_sample_number_ = 0;
    std::atomic<size_t> remaining_initial_samples_;
    absl::Mutex reservoir_sample_lock_;

    // This area is used to allocate TraceContext objects for packed samples:
    google::protobuf::Arena sample_arena_;

    // If true this entire action list was triggered by a warmup RPC, so all
    // actions it initiates will propgate the warmup flag:
    bool warmup_;
    std::atomic<int> pending_action_count_ = 0;
  };

  absl::Status InitializeTables();
  absl::Status InitializePayloadsMap();
  absl::Status InitializeRpcFanoutFilter(RpcDefinition& rpc_def);
  absl::Status InitializeRpcDefinitionsMap();
  absl::Status InitializeActivityConfigMap();
  absl::Status ParseActivityConfig(ActivityConfig& ac);

  void RunActionList(int list_index, const ServerRpcState* incoming_rpc_state,
                     bool force_warmup = false);
  void RunAction(ActionState* action_state);
  void StartOpenLoopIteration(ActionState* action_state);
  void RunActivity(ActionState* action_state);
  void StartIteration(std::shared_ptr<ActionIterationState> iteration_state);
  void FinishIteration(std::shared_ptr<ActionIterationState> iteration_state);

  void RunRpcActionIteration(
      std::shared_ptr<ActionIterationState> iteration_state);
  std::vector<int> PickRpcFanoutTargets(ActionState* action_state);

  void AddActivityLogs(ServicePerformanceLog* sp_log);

  std::atomic<int64_t> consume_cpu_iteration_cnt_ = 0;

  std::unique_ptr<SimulatedClientRpc[]> client_rpc_table_;
  std::vector<SimulatedServerRpc> server_rpc_table_;
  std::vector<ActionListTableEntry> action_lists_;

  absl::Status ConnectToPeers();
  std::function<void()> RpcHandler(ServerRpcState* state);

  int get_payload_size(const std::string& name);

  SafeNotification canceled_;
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
  std::string engine_name_;

  // Payloads definitions
  std::map<std::string, PayloadSpec> payload_map_;
  std::map<std::string, RpcDefinition> rpc_map_;
  std::map<std::string, int> activity_config_indices_map_;
  std::vector<ParsedActivityConfig> stored_activity_config_;

  // The first index is the service, the second is the instance.
  std::vector<std::vector<PeerMetadata>> peers_;
  int trace_id_ = -1;
  SimpleClock* clock_ = nullptr;

  // Random
  absl::BitGen random_generator;

  std::atomic<int64_t> detached_actionlist_threads_ = 0;
  absl::Mutex cumulative_activity_log_mu_;
  std::map<std::string, std::map<std::string, int64_t>>
      cumulative_activity_logs_;

  std::map<std::string, int> sample_generator_indices_map_;
  std::vector<std::unique_ptr<DistributionSampleGenerator>>
      sample_generator_array_;

  // Allocate Sample generator if the distribution configuration is
  // valid. Return an error if the config is invalid of if same config
  // is defined more than once. Add the unique_ptr for allocated sample
  // generator to the sample_interpretor_array_ and store the index in
  // sample_interpretor_indices_map_.
  absl::Status AllocateAndInitializeSampleGenerators();

  // Get the index of the sample interpretor in sample_generator_array_
  int GetSampleGeneratorIndex(const std::string& name);

  // Populate the common_request with samples generated from
  // the sample generator. For values not generated by sample
  // generator, default values are used.
  void PopulateCommonRequest(GenericRequest* common_request,
                             const std::vector<int>& sample,
                             const RpcDefinition& rpc_def);

  // Get canonical version of DistributionConfig from the config
  // provided by the user. The canonical version is then used to
  // generate sample with 'kMaxFieldNames' dimensions.
  absl::StatusOr<DistributionConfig> GetCanonicalConfig(
      const DistributionConfig& input_config);

  enum kFieldNames {
    kRequestPayloadSize = 0,
    kResponsePayloadSize = 1,
    kMaxFieldNames = 2,
  };
};

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_ENGINE_H_
