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
#include "traffic_config.pb.h"
#include "absl/status/statusor.h"
#include "google/protobuf/stubs/status_macros.h"
#include "grpc_wrapper.h"

namespace std{
ostream& operator<< (ostream &out, grpc::Status const& c);
};

namespace distbench {

void set_use_ipv4_first(bool _use_ipv4_first);

grpc::ChannelArguments DistbenchCustomChannelArguments();
std::shared_ptr<grpc::ChannelCredentials> MakeChannelCredentials();
std::shared_ptr<grpc::ServerCredentials> MakeServerCredentials();
std::string IpAddressForDevice(std::string_view netdev);
std::string SocketAddressForDevice(std::string_view netdev, int port);
std::thread RunRegisteredThread(const std::string& thread_name,
                                std::function<void()> f);

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

void InitLibs(const char* argv0);

std::string Hostname();

std::vector<std::string> SummarizeTestResult(const TestResult& test_result);

grpc::Status Annotate(const grpc::Status& status, std::string_view context);

grpc::Status abslStatusToGrpcStatus(const absl::Status &status);
absl::Status grpcStatusToAbslStatus(const grpc::Status &status);

absl::StatusOr<std::string> ReadFileToString(const std::string &filename);
}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_UTILS_H_
