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
#include <thread>

#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "distbench.pb.h"
#include "distbench_netutils.h"
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

grpc::ChannelArguments DistbenchCustomChannelArguments();
std::shared_ptr<grpc::ChannelCredentials> MakeChannelCredentials();
std::shared_ptr<grpc::ServerCredentials> MakeServerCredentials();

std::string ServiceInstanceName(std::string_view service_type, int instance);
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

void AddServerStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value);

void AddClientStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value);

void AddActivitySettingIntTo(ActivityConfig* ac, std::string option_name,
                             int value);

void AddActivitySettingStringTo(ActivityConfig* ac, std::string option_name,
                                std::string value);

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_UTILS_H_
