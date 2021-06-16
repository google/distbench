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

#ifndef DISTBENCH_DISTBENCH_UTILS_H_
#define DISTBENCH_DISTBENCH_UTILS_H_

#include <memory>
#include <thread>

#include "distbench.pb.h"
#include "dstmf.pb.h"
#include "absl/status/statusor.h"
#include "google/protobuf/stubs/status_macros.h"
#include "grpc_wrapper.h"

namespace std{
ostream& operator<< (ostream &out, grpc::Status const& c);
};

namespace distbench {

void set_use_ipv4_first(bool _use_ipv4_first);

std::shared_ptr<grpc::ChannelCredentials> MakeChannelCredentials();
std::shared_ptr<grpc::ServerCredentials> MakeServerCredentials();
std::string IpAddressForDevice(std::string_view netdev);
std::string SocketAddressForDevice(std::string_view netdev, int port);
std::thread RunRegisteredThread(const std::string& thread_name,
                                std::function<void()> f);
int AllocatePort();
void FreePort(int port);

std::string ServiceInstanceName(std::string_view service_type, int instance);
std::map<std::string, int> EnumerateServiceSizes(
    const DistributedSystemDescription& config);
std::map<std::string, int> EnumerateServiceInstanceIds(
    const DistributedSystemDescription& config);
std::map<std::string, int> EnumerateServiceTypes(
    const DistributedSystemDescription& config);
std::map<std::string, int> EnumerateRpcs(
    const DistributedSystemDescription& config);
ServiceSpec GetServiceSpec(std::string_view name,
                           const DistributedSystemDescription& config);

void InitLibs();

std::string Hostname();

std::string SummarizeTestResult(const TestResult& test_result);

grpc::Status Annotate(const grpc::Status& status, std::string_view context);

grpc::Status abslStatusToGrpcStatus(const absl::Status &status);
absl::Status grpcStatusToAbslStatus(const grpc::Status &status);
}  // namespace distbench


#if 0
#define ASSIGN_OR_RETURN_IMPL(status, lhs, rexpr) \
  absl::Status status = DoAssignOrReturn(lhs, (rexpr)); \
      if (ABSL_PREDICT_FALSE(!status.ok())) return status;

template<typename T>
absl::Status DoAssignOrReturn(T& lhs, absl::StatusOr<T> result) {
    if (result.ok()) {
          lhs = result.ValueOrDie();
            }
      return result.status();
}

#define ASSIGN_OR_RETURN(lhs, rexpr) \
  ASSIGN_OR_RETURN_IMPL( \
      (_status_or_value), lhs, rexpr);

#define RETURN_IF_ERROR(expr)                                                \
  do {                                                                       \
    /* Using _status below to avoid capture problems if expr is "status". */ \
    const absl::Status _status = (expr);              \
    if (ABSL_PREDICT_FALSE(!_status.ok())) return _status;               \
  } while (0)
#endif

#endif  // DISTBENCH_DISTBENCH_UTILS_H_
