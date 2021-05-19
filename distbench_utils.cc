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

#include "distbench_utils.h"

#include "interface_lookup.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_cat.h"
#include <glog/logging.h>
#include <grpcpp/security/credentials.h>
#include "base/logging.h"

namespace std {
ostream& operator<< (ostream &out, grpc::Status const& c)
{
    return out << "(grpc::status" << c.error_message() << ")";
}
}

namespace distbench {

std::shared_ptr<grpc::ChannelCredentials> MakeChannelCredentials() {
  // grpc::SslCredentialsOptions sec_ops;
  // return grpc::SslCredentials(sec_ops);
  return grpc::InsecureChannelCredentials();
}

std::shared_ptr<grpc::ServerCredentials> MakeServerCredentials() {
  // grpc::SslServerCredentialsOptions sec_ops;
  // return grpc::SslServerCredentials(sec_ops);
  return grpc::InsecureServerCredentials();
}

std::thread RunRegisteredThread(const std::string& thread_name,
                                std::function<void()> f) {
  return std::thread([=]() {
    f();
  });
}

int AllocatePort() {
  return net_util::PickUnusedPortOrDie();
}

void FreePort(int port) {
  net_util::RecycleUnusedPort(port);
}

void InitLibs() {
  // Extra library initialization can go here
}

std::string IpAddressForDevice(std::string_view netdev) {
  net_base::IPAddress ip;
  CHECK(net_base::InterfaceLookup::MyIPv6Address(&ip) ||
        net_base::InterfaceLookup::MyIPv4Address(&ip));
  return ip.ToString();
}

std::string SocketAddressForDevice(std::string_view netdev, int port) {
  net_base::IPAddress ip;
  if (net_base::InterfaceLookup::MyIPv6Address(&ip)) {
    return absl::StrCat("[", ip.ToString(), "]:", port);
  } else if (net_base::InterfaceLookup::MyIPv4Address(&ip)) {
    return absl::StrCat(ip.ToString(), ":", port);
  }
  LOG(QFATAL) << "Could not get ip v4/v6 address";
  exit(1);
}

std::string ServiceInstanceName(std::string_view service_type, int instance) {
  QCHECK(!service_type.empty());
  QCHECK_GE(instance, 0);
  return absl::StrCat(service_type, "/", instance);
}

std::map<std::string, int> EnumerateServiceTypes(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    // LOG(INFO) << "service " << service.server_type() << " = " << ret.size();
    ret[service.server_type()] = ret.size();
  }
  return ret;
}

std::map<std::string, int> EnumerateServiceSizes(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    // LOG(INFO) << "service " << service.server_type() << " = " << ret.size();
    ret[service.server_type()] = service.count();
  }
  return ret;
}

std::map<std::string, int> EnumerateRpcs(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& rpc : config.rpc_descriptions()) {
    ret[rpc.name()] = ret.size();
  }
  return ret;
}

std::map<std::string, int> EnumerateServiceInstanceIds(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    for (int i = 0; i < service.count(); ++i) {
      std::string instance = ServiceInstanceName(service.server_type(), i);
      // LOG(INFO) << "service " << instance << " = " << ret.size();
      ret[instance] = ret.size();
    }
  }
  return ret;
}

ServiceSpec GetServiceSpec(std::string_view name,
                           const DistributedSystemDescription& config) {
  for (const auto& service : config.services()) {
    if (service.server_type() == name) {
      return service;
    }
  }
  LOG(QFATAL) << "Service not found: " << name;
  exit(1);
}

std::string GetClientServiceName(const RpcSpec& rpc_spec) {
  std::vector<std::string> rpc_name_parts =
    absl::StrSplit(rpc_spec.name(), absl::MaxSplits('/', 1));
  return rpc_name_parts[0];
}

std::string GetServerServiceName(const RpcSpec& rpc_spec) {
  std::vector<std::string> handler_name_parts =
    absl::StrSplit(rpc_spec.handler_action_list_name(),
                   absl::MaxSplits('/', 1));
  return handler_name_parts[0];
}


}  // namespace distbench
