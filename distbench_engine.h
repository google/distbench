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

#ifndef DISTBENCH_DISTBENCH_ENGINE_H_
#define DISTBENCH_DISTBENCH_ENGINE_H_

#include <unordered_set>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/random/random.h"
#include "activity.h"
#include "distbench.grpc.pb.h"
#include "distbench_payload.h"
#include "distbench_thread_support.h"
#include "distbench_threadpool.h"
#include "distbench_utils.h"
#include "joint_distribution_sample_generator.h"
#include "protocol_driver.h"

namespace distbench {

class ThreadSafeDictionary {
 public:
  ThreadSafeDictionary();
  ~ThreadSafeDictionary() = default;
  int GetIndex(std::string_view text);
  std::string_view GetValue(int index) const;
  std::vector<std::string> GetContents();

 private:
  std::vector<std::string> contents_;
  absl::flat_hash_map<std::string, size_t> contents_map_;
  mutable absl::Mutex mutex_;
};

class DistBenchEngine : public ConnectionSetup::Service {
 public:
  explicit DistBenchEngine(std::unique_ptr<ProtocolDriver> pd);
  ~DistBenchEngine() override;

  absl::Status Initialize(
      const DistributedSystemDescription& global_description,
      std::string_view control_plane_device, std::string_view service_name,
      GridIndex service_index, int* port);

  absl::Status ConfigurePeers(const ServiceEndpointMap& peers);
  absl::Status RunTraffic(const RunTrafficRequest* request);
  void CancelTraffic(absl::Status status,
                     absl::Duration grace_period = absl::ZeroDuration());

  void FinishTraffic();
  ServicePerformanceLog GetLogs();

  grpc::Status SetupConnection(grpc::ServerContext* context,
                               const ConnectRequest* request,
                               ConnectResponse* response) override;

 private:
  DistBenchEngine() = default;
  friend absl::Status ValidateTrafficConfig(
      const DistributedSystemDescription& traffic_config);

  struct StochasticDist {
    float probability;
    int nb_targets;
  };

  enum FanoutFilter {
    kAll = 0,
    kSameX = 1 << 0,
    kSameY = 1 << 1,
    kSameZ = 1 << 2,
    kSameXY = kSameX | kSameY,
    kSameXZ = kSameX | kSameZ,
    kSameYZ = kSameY | kSameZ,
    kSameXYZ = kSameX | kSameY | kSameZ,
    kRingX,
    kRingY,
    kRingZ,
    kAlternatingRingX,
    kAlternatingRingY,
    kAlternatingRingZ,
    kLinearX,
    kLinearY,
    kLinearZ,
    kRandomSingle,
    kRoundRobin,
    kStochastic,
  };

  struct RpcDefinition {
    // Original proto
    RpcSpec rpc_spec;

    ServiceSpec server_service_spec;

    // Used to store decoded stochastic fanout
    FanoutFilter fanout_filter = kAll;
    int fanout_filter_distance = 0;
    std::vector<StochasticDist> stochastic_dist;

    // If there is a joint distribution defined we use that:
    int joint_sample_generator_index = -1;

    // If there are univariant distributions defined we use those:
    int request_payload_index = -1;
    int response_payload_index = -1;

    // If there is are fixed sizes defined we use those:
    ssize_t request_payload_size = -1;
    ssize_t response_payload_size = -1;

    bool allowed_from_this_client = false;
    int multiserver_channel_index = -1;
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
    std::vector<GenericRequestResponse> response_table;
    int handler_actionlist_index = -1;
    RpcDefinition rpc_definition;
    bool allowed = false;
  };

  struct SimulatedClientRpc {
    int service_index;
    std::vector<GenericRequestResponse> request_table;
    RpcDefinition rpc_definition;
    std::atomic<int64_t> rpc_tracing_counter = 0;
    std::vector<int> pending_requests_per_peer;
  };

  struct ActionTableEntry {
    Action proto;
    // index into peers_ that identifies the target(s) of this RPC
    int rpc_service_index = -1;
    int response_payload_override_index = -1;
    // index into the client_rpc_table_/server_rpc_table_;
    int rpc_index = -1;
    int rpc_replay_trace_index = -1;
    int actionlist_index = -1;
    int activity_config_index = -1;
    int delay_distribution_index = -1;
    std::vector<int> dependent_action_indices;
    int request_payload_override_index = -1;
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
    absl::BitGen rand_gen;
  };

  struct ActionState {
    bool started = false;
    bool finished = false;
    bool waiting_for_delay = false;
    bool skipped = false;
    ActionListState* actionlist_state;

    absl::Mutex iteration_mutex;
    int next_iteration ABSL_GUARDED_BY(iteration_mutex);
    int finished_iterations ABSL_GUARDED_BY(iteration_mutex);
    absl::Time next_iteration_time = absl::InfiniteFuture();
    absl::Time warmup_done_time = absl::InfinitePast();

    int64_t iteration_limit = std::numeric_limits<int64_t>::max();
    absl::Time time_limit = absl::InfiniteFuture();

    const ActionTableEntry* action = nullptr;
    int rpc_index;
    int rpc_service_index;
    int action_index;

    std::function<void(std::shared_ptr<ActionIterationState> iteration_state)>
        iteration_function;
    std::function<void(void)> all_done_callback;

    std::unique_ptr<Activity> activity;

    std::exponential_distribution<double> exponential_gen;
    bool interval_is_exponential;

    // These are indices into the payloads_config array:
    int request_payload_override_index = -1;
    int response_payload_override_index = -1;
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
    int error_index;
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
                       ClientRpcState* state, absl::BitGenRef bitgen);
    void RecordPackedLatency(size_t sample_number, size_t index,
                             size_t rpc_index, size_t service_type,
                             size_t instance, ClientRpcState* state);
    void UnpackLatencySamples();

    int GetOverrideIndex() const {
      absl::MutexLock m(&action_mu);
      return response_payload_override_index;
    }

    int response_payload_override_index ABSL_GUARDED_BY(action_mu) = -1;
    int actionlist_index = -1;
    int actionlist_invocation = -1;
    ServerRpcState* incoming_rpc_state = nullptr;
    std::unique_ptr<ActionState[]> state_table;
    const ActionListTableEntry* action_list;
    mutable absl::Mutex action_mu;
    std::vector<int> finished_action_indices;

    std::vector<std::vector<PeerPerformanceLog>> peer_logs_
        ABSL_GUARDED_BY(action_mu);
    std::unique_ptr<PackedLatencySample[]> packed_samples_;
    size_t packed_samples_size_ = 0;
    std::atomic<size_t> packed_sample_number_ = 0;
    std::atomic<size_t> remaining_initial_samples_;
    absl::Mutex reservoir_sample_lock_;

    // This area is used to allocate TraceContext objects for packed samples:
    ::google::protobuf::Arena sample_arena_;

    // If true this entire action list was triggered by a warmup RPC, so all
    // actions it initiates will propgate the warmup flag:
    bool warmup_;
    std::atomic<int> pending_action_count_ = 0;
    std::shared_ptr<ThreadSafeDictionary> actionlist_error_dictionary_;
    absl::flat_hash_set<std::string> predicates_;
  };

  absl::Status InitializeConfig(
      const DistributedSystemDescription& global_description,
      std::string_view service_name, GridIndex service_index);
  absl::Status InitializeTables();
  absl::Status InitializePayloadsMap();
  absl::Status InitializeRpcFanoutFilter(RpcDefinition& rpc_def);
  absl::Status InitializeRpcDefinitionsMap();
  absl::Status InitializeActivityConfigMap();
  absl::Status InitializeRpcTraceMap();

  void RunActionList(int actionlist_index, ServerRpcState* incoming_rpc_state,
                     bool force_warmup = false);

  RpcReplayTraceLog RunRpcReplayTrace(
      int rpc_replay_trace_index, ServerRpcState* incoming_rpc_state,
      std::shared_ptr<ThreadSafeDictionary> actionlist_error_dictionary,
      bool force_warmup);

  void InitiateAction(ActionState* action_state);
  void StartOpenLoopIteration(ActionState* action_state,
                              absl::BitGenRef bitgen);
  void RunActivity(ActionState* action_state);
  void StartNewIterations(ActionState* action_state,
                          int starting_iteration_number, int count);
  void StartIteration(std::shared_ptr<ActionIterationState> iteration_state);
  void FinishIteration(std::shared_ptr<ActionIterationState> iteration_state);

  std::shared_ptr<ActionIterationState> AllocIterationState()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(iteration_state_cache_mtx_);
  void FreeIterationState(std::shared_ptr<ActionIterationState> state);

  void RunRpcActionIterationCommon(
      std::shared_ptr<ActionIterationState> iteration_state,
      std::vector<int> targets, bool multiserver);

  void RunRpcActionIteration(
      std::shared_ptr<ActionIterationState> iteration_state);
  void RunMultiServerChannelRpcActionIteration(
      std::shared_ptr<ActionIterationState> iteration_state);
  std::vector<int> PickRpcFanoutTargets(ActionIterationState* iteration_state);

  void AddActivityLogs(ServicePerformanceLog* sp_log);
  void AddRpcReplayTraceLogs(ServicePerformanceLog* sp_log);

  std::atomic<int64_t> consume_cpu_iteration_cnt_ = 0;

  std::unique_ptr<SimulatedClientRpc[]> client_rpc_table_;
  std::vector<SimulatedServerRpc> server_rpc_table_;
  std::vector<ActionListTableEntry> action_lists_;

  std::vector<int> PickGridTargets(FanoutFilter filter,
                                   const ServiceSpec& peer_service);
  std::vector<int> PickRingTargets(FanoutFilter filter,
                                   const ServiceSpec& peer_service);
  std::vector<int> PickLinearTargets(FanoutFilter filter, int distance,
                                     const ServiceSpec& peer_service);

  size_t ComputeResponseSize(ServerRpcState* state, int override_index);

  absl::Status ConnectToPeers();
  std::function<void()> RpcHandler(ServerRpcState* state);

  // Allocate Sample generator if the distribution configuration is
  // valid. Return an error if the config is invalid of if same config
  // is defined more than once. Add the unique_ptr for allocated sample
  // generator to the rpc_distribution_generators_ and store the index in
  // sample_generator_indices_map_.
  absl::Status InitializeSampleGenerators();

  // Get the index of the sample generator in rpc_distribution_generators_
  int GetSizeSampleGeneratorIndex(const std::string& name);
  int GetRpcSampleGeneratorIndex(const std::string& name);
  int GetDelaySampleGeneratorIndex(const std::string& name);
  int GetPayloadSize(const std::string& name);
  int GetPayloadSizeIndex(const std::string& name);

  DistributedSystemDescription traffic_config_;
  ServiceEndpointMap service_map_;
  std::string service_name_;
  ServiceSpec service_spec_;
  int64_t traffic_start_time_ns_ = 0;
  std::set<std::string> dependent_services_;
  int service_index_;
  int service_instance_;
  GridIndex grid_index_ = {0, 0, 0};
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<ProtocolDriver> pd_;
  std::thread engine_main_thread_;
  std::string engine_name_;
  std::map<std::string, int> service_index_map_;

  // Payloads definitions
  std::map<std::string, PayloadSpec> payload_map_;
  std::map<std::string, RpcDefinition> rpc_map_;
  std::map<std::string, int> activity_config_indices_map_;
  std::map<std::string, int> rpc_replay_trace_indices_map_;
  std::vector<ParsedActivityConfig> stored_activity_config_;

  std::unique_ptr<PayloadAllocator> payload_allocator_;

  std::unique_ptr<std::atomic<int>[]> actionlist_invocation_counts;

  // The first index is the service, the second is the instance.
  std::vector<std::vector<PeerMetadata>> peers_;
  int trace_id_ = -1;
  SimpleClock* clock_ = nullptr;

  std::atomic<int64_t> pending_rpcs_ = 0;
  std::atomic<int64_t> detached_actionlist_threads_ = 0;
  absl::Mutex cumulative_activity_log_mu_;
  std::map<std::string, std::map<std::string, int64_t>>
      cumulative_activity_logs_;

  absl::Mutex replay_logs_mutex;
  std::vector<std::unique_ptr<RpcReplayTraceLog>> replay_logs_
      ABSL_GUARDED_BY(replay_logs_mutex);

  std::map<std::string, int> delay_distribution_generators_map_;
  std::vector<std::unique_ptr<DistributionSampleGenerator>>
      delay_distribution_generators_;

  std::map<std::string, int> rpc_distribution_generators_map_;
  std::vector<std::unique_ptr<DistributionSampleGenerator>>
      rpc_distribution_generators_;

  std::map<std::string, int> size_distribution_generators_map_;
  std::vector<std::unique_ptr<DistributionSampleGenerator>>
      size_distribution_generators_;

  absl::Mutex cancelation_mutex_;
  std::string cancelation_reason_;
  SafeNotification canceled_;
  absl::Time cancelation_time_;

  std::unique_ptr<AbstractThreadpool> thread_pool_;
  std::shared_ptr<ThreadSafeDictionary> actionlist_error_dictionary_;

  std::vector<std::shared_ptr<ActionIterationState>> iteration_state_cache_
      ABSL_GUARDED_BY(iteration_state_cache_mtx_);
  mutable absl::Mutex iteration_state_cache_mtx_;

  absl::BitGen engine_bitgen_;
  mutable absl::Mutex engine_bitgen_mtx_;
};

// This function tests if traffic_config could initialize tables in
// DistBenchEngine. Used by distbench_busybox.cc in MainCheckTest().
absl::Status ValidateTrafficConfig(
    const DistributedSystemDescription& traffic_config);

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_ENGINE_H_
