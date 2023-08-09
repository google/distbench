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

#ifndef DISTBENCH_DISTBENCH_UTILS_H_
#define DISTBENCH_DISTBENCH_UTILS_H_

#include <sys/resource.h>

#include <memory>

#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "distbench.pb.h"
#include "google/protobuf/stubs/status_macros.h"
#include "grpc_wrapper.h"
#include "traffic_config.pb.h"

namespace std {
ostream& operator<<(ostream& out, grpc::Status const& c);
};

namespace distbench {

// This is to allow Notify to be called from multiple threads safely:
// This scenario happens when unit tests timeout and obscure why they
// failed by crashing.
class SafeNotification {
 public:
  // Returns true if Notify was not already called.
  bool TryToNotify() {
    if (!n.HasBeenNotified()) {
      absl::MutexLock m(&mu);
      if (!n.HasBeenNotified()) {
        n.Notify();
        return true;
      }
    }
    return false;
  }
  bool HasBeenNotified() { return n.HasBeenNotified(); }
  void WaitForNotification() { n.WaitForNotification(); }

 private:
  absl::Mutex mu;
  absl::Notification n;
};

struct GridIndex {
  int x;
  int y;
  int z;

  bool operator==(const GridIndex& other) const {
    return x == other.x && y == other.y && z == other.z;
  }

  bool operator!=(const GridIndex& other) const { return !(*this == other); }
};

GridIndex GetGridIndexFromInstance(const distbench::ServiceSpec& service_spec,
                                   int instance);

int GetInstanceFromGridIndex(const distbench::ServiceSpec& service_spec,
                             GridIndex index);

std::string GetInstanceName(const distbench::ServiceSpec& service_spec,
                            int instance);

GridIndex GetGridIndexFromName(std::string_view name);

grpc::ChannelArguments DistbenchCustomChannelArguments();
std::shared_ptr<grpc::ChannelCredentials> MakeChannelCredentials();
std::shared_ptr<grpc::ServerCredentials> MakeServerCredentials();

std::map<std::string, int> EnumerateServiceSizes(
    const DistributedSystemDescription& config);
std::map<std::string, int> EnumerateServiceInstanceIds(
    const DistributedSystemDescription& config);
std::map<std::string, int> EnumerateServiceTypes(
    const DistributedSystemDescription& config);
std::map<std::string, int> EnumerateRpcs(
    const DistributedSystemDescription& config);
absl::StatusOr<ServiceSpec> GetServiceSpec(
    std::string_view name, const DistributedSystemDescription& config);

void InitLibs(const char* argv0);

std::string Hostname();

grpc::Status Annotate(const grpc::Status& status, std::string_view context);

grpc::Status abslStatusToGrpcStatus(const absl::Status& status);
absl::Status grpcStatusToAbslStatus(const grpc::Status& status);

void SetGrpcClientContextDeadline(grpc::ClientContext* context, int max_time_s);

absl::StatusOr<std::string> ReadFileToString(const std::string& filename);

void ApplyServerSettingsToGrpcBuilder(grpc::ServerBuilder* builder,
                                      const ProtocolDriverOptions& pd_opts);

// RUsage functions
RUsage StructRUsageToMessage(const struct rusage& s_rusage);
RUsage DiffStructRUsageToMessage(const struct rusage& start,
                                 const struct rusage& end);

RUsageStats GetRUsageStatsFromStructs(const struct rusage& start,
                                      const struct rusage& end);
struct rusage DoGetRusage();

std::string GetNamedSettingString(
    const ::google::protobuf::RepeatedPtrField<distbench::NamedSetting>&
        settings,
    absl::string_view name, std::string default_value);

std::string GetNamedServerSettingString(
    const distbench::ProtocolDriverOptions& opts, absl::string_view name,
    std::string default_value);

std::string GetNamedClientSettingString(
    const distbench::ProtocolDriverOptions& opts, absl::string_view name,
    std::string default_value);

int64_t GetNamedSettingInt64(const ::google::protobuf::RepeatedPtrField<
                                 distbench::NamedSetting>& settings,
                             absl::string_view name, int64_t default_value);

int64_t GetNamedServerSettingInt64(const distbench::ProtocolDriverOptions& opts,
                                   absl::string_view name,
                                   int64_t default_value);

int64_t GetNamedClientSettingInt64(const distbench::ProtocolDriverOptions& opts,
                                   absl::string_view name,
                                   int64_t default_value);

absl::StatusOr<int64_t> GetNamedAttributeInt64(
    const distbench::DistributedSystemDescription& test, absl::string_view name,
    int64_t default_value);

// Parse/Read TestSequence protos.
absl::StatusOr<distbench::TestSequence> ParseTestSequenceTextProto(
    const std::string& text_proto);
absl::StatusOr<TestSequence> ParseTestSequenceProtoFromFile(
    const std::string& filename);

// Write TestSequenceResults protos.
absl::Status SaveResultProtoToFile(
    const std::string& filename, const distbench::TestSequenceResults& result);
absl::Status SaveResultProtoToFileBinary(
    const std::string& filename, const distbench::TestSequenceResults& result);

void AddServerInt64OptionTo(ProtocolDriverOptions& pdo, std::string option_name,
                            int64_t value);

void AddServerStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value);

void AddClientStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value);

void AddActivitySettingIntTo(ActivityConfig* ac, std::string option_name,
                             int value);

void AddActivitySettingStringTo(ActivityConfig* ac, std::string option_name,
                                std::string value);

absl::StatusOr<distbench::ServiceSpec> GetCanonicalServiceSpec(
    const distbench::ServiceSpec& service_spec);

absl::StatusOr<distbench::DistributedSystemDescription>
GetCanonicalDistributedSystemDescription(
    const distbench::DistributedSystemDescription& traffic_config);

absl::StatusOr<distbench::TestSequence> GetCanonicalTestSequence(
    const distbench::TestSequence& sequence);

// canonical_fields is a nullptr-terminated array of field names in the
// desired order.
absl::StatusOr<DistributionConfig> GetCanonicalDistributionConfig(
    const DistributionConfig& input_config, const char* canonical_fields[]);

// canonical_fields list the field names in the desired order.
absl::StatusOr<DistributionConfig> GetCanonicalDistributionConfig(
    const DistributionConfig& input_config,
    std::vector<std::string_view> canonical_fields);

absl::Status ValidateRpcReplayTrace(const RpcReplayTrace& trace,
                                    std::map<std::string, int> service_sizes);

absl::Status ValidateDistributionConfig(const DistributionConfig& config);

absl::Status ValidateTestsSetting(const TestsSetting& settings);

ServiceBundle AllServiceInstances(
    const DistributedSystemDescription& traffic_config);

// Functions to check constraints against attributes
bool CheckConstraintList(
    const ConstraintList& constraint_list,
    const google::protobuf::RepeatedPtrField<Attribute>& attributes);

bool CheckConstraintSet(
    const ConstraintSet& constraint_set,
    const google::protobuf::RepeatedPtrField<Attribute>& attributes);

bool CheckConstraint(
    const Constraint& constraint,
    const google::protobuf::RepeatedPtrField<Attribute>& attributes);

bool CheckIntConstraint(const int64_t& int_value, const Attribute& attribute,
                        const Constraint& constraint);

bool CheckStringConstraint(const std::string& string_value,
                           const Attribute& attribute,
                           const Constraint& constraint);

template <typename T>
void SetSerializedSize(T* msg, size_t target_size) {
  if (target_size == msg->ByteSizeLong()) {
    return;
  }
  msg->clear_payload();
  msg->clear_trim();
  if (target_size > msg->ByteSizeLong()) {
    msg->set_payload("");
    ssize_t pad = target_size - msg->ByteSizeLong();
    if (pad > 0) {
      msg->set_payload(std::string(pad, 'D'));
      while (msg->ByteSizeLong() != target_size) {
        if (msg->ByteSizeLong() < target_size) {
          msg->set_trim(false);
        }
        pad += target_size - msg->ByteSizeLong();
        msg->mutable_payload()->resize(pad, 'D');
      }
    }
  }
}

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_UTILS_H_
