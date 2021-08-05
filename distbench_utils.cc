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

#include <sys/resource.h>

#include <cerrno>
#include <fstream>
#include <streambuf>

#include "interface_lookup.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "glog/logging.h"

#include "distbench_netutils.h"

namespace std {
ostream& operator<< (ostream &out, grpc::Status const& c)
{
    return out << "(grpc::status: " << c.error_message() << ")";
}
}

namespace distbench {

static bool use_ipv4_first = false;

void set_use_ipv4_first(bool _use_ipv4_first) {
  use_ipv4_first = _use_ipv4_first;
}

std::string Hostname() {
  char hostname[4096] = {};
  if (gethostname(hostname, sizeof(hostname))) {
    LOG(ERROR) << errno;
  }
  return hostname;
}

grpc::ChannelArguments DistbenchCustomChannelArguments() {
  grpc::ChannelArguments args;
  args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
              std::numeric_limits<int32_t>::max());
  return args;
}

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

void InitLibs(const char* argv0) {
  // Extra library initialization can go here
  ::google::InitGoogleLogging(argv0);
  GOOGLE_PROTOBUF_VERIFY_VERSION;
}

std::string IpAddressForDevice(std::string_view netdev) {
  DeviceIpAddress address = GetBestAddress(use_ipv4_first, netdev);
  return address.ip();
}

std::string SocketAddressForDevice(std::string_view netdev, int port) {
  DeviceIpAddress address = GetBestAddress(use_ipv4_first, netdev);
  if (address.isIPv4())
    return absl::StrCat(address.ip(), ":", port);

  return absl::StrCat("[", address.ip(), "]:", port);
}

std::string ServiceInstanceName(std::string_view service_type, int instance) {
  CHECK(!service_type.empty());
  CHECK_GE(instance, 0);
  return absl::StrCat(service_type, "/", instance);
}

std::map<std::string, int> EnumerateServiceTypes(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    // LOG(INFO) << "service " << service.name() << " = " << ret.size();
    ret[service.name()] = ret.size();
  }
  return ret;
}

std::map<std::string, int> EnumerateServiceSizes(
    const DistributedSystemDescription& config) {
  std::map<std::string, int> ret;
  for (const auto& service : config.services()) {
    // LOG(INFO) << "service " << service.name() << " = " << ret.size();
    ret[service.name()] = service.count();
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
      std::string instance = ServiceInstanceName(service.name(), i);
      // LOG(INFO) << "service " << instance << " = " << ret.size();
      ret[instance] = ret.size();
    }
  }
  return ret;
}

ServiceSpec GetServiceSpec(std::string_view name,
                           const DistributedSystemDescription& config) {
  for (const auto& service : config.services()) {
    if (service.name() == name) {
      return service;
    }
  }
  LOG(FATAL) << "Service not found: " << name;
}

grpc::Status Annotate(const grpc::Status& status, std::string_view context) {
  return grpc::Status(
      status.error_code(), absl::StrCat(context, status.error_message()));
}

grpc::Status abslStatusToGrpcStatus(const absl::Status &status) {
  if (status.ok())
    return grpc::Status::OK;

  std::string message = std::string(status.message());
  // GRPC and ABSL (currently) share the same error codes
  grpc::StatusCode code = (grpc::StatusCode)status.code();
  return grpc::Status(code, message);
}

absl::Status grpcStatusToAbslStatus(const grpc::Status &status) {
  if (status.ok())
    return absl::OkStatus();

  std::string message = status.error_message();
  // GRPC and ABSL (currently) share the same error codes
  absl::StatusCode code = (absl::StatusCode)status.error_code();
  return absl::Status(code, message);
}

absl::StatusOr<std::string> ReadFileToString(const std::string &filename) {
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (!in) {
    std::string error_message{"Error reading input file:" + filename + "; "};
    error_message += std::strerror(errno);
    return absl::InvalidArgumentError(error_message);
  }

  std::istreambuf_iterator<char> it(in);
  std::istreambuf_iterator<char> end;
  std::string str(it, end);

  in.close();
  return str;
}

void ApplyServerSettingsToGrpcBuilder(grpc::ServerBuilder *builder,
    const ProtocolDriverOptions &pd_opts) {
  for (const auto &setting: pd_opts.server_settings()) {
    if (!setting.has_name()) {
      LOG(INFO) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto &name = setting.name();
    if (setting.has_str_value()) {
      LOG(INFO) << "ProtocolDriverOptions.NamedSetting[" << name << "]="
                << setting.str_value();
      builder->AddChannelArgument(name, setting.str_value());
      continue;
    }
    if (setting.has_int_value()) {
      LOG(INFO) << "ProtocolDriverOptions.NamedSetting[" << name << "]="
                << setting.int_value();
      builder->AddChannelArgument(name, setting.int_value());
      continue;
    }

    LOG(INFO) << "ProtocolDriverOptions.NamedSetting[" << name << "]"
              << " not setting found (str or int)!";
  }
}

// RUsage functions
namespace {
double TimevalToDouble(const struct timeval &t){
  return (double)t.tv_usec / 1000000.0 + t.tv_sec;
}
};  // Anonymous namespace

RUsage StructRUsageToMessage(const struct rusage &s_rusage) {
  RUsage rusage;

  rusage.set_user_cpu_time(TimevalToDouble(s_rusage.ru_utime));
  rusage.set_system_cpu_time(TimevalToDouble(s_rusage.ru_stime));
  rusage.set_max_resident_set_size(s_rusage.ru_maxrss);
  rusage.set_integral_shared_memory_size(s_rusage.ru_ixrss);
  rusage.set_integral_unshared_data_size(s_rusage.ru_idrss);
  rusage.set_integral_unshared_stack_size(s_rusage.ru_isrss);
  rusage.set_page_reclaims_soft_page_faults(s_rusage.ru_minflt);
  rusage.set_page_faults_hard_page_faults(s_rusage.ru_majflt);
  rusage.set_swaps(s_rusage.ru_nswap);
  rusage.set_block_input_operations(s_rusage.ru_inblock);
  rusage.set_block_output_operations(s_rusage.ru_oublock);
  rusage.set_ipc_messages_sent(s_rusage.ru_msgsnd);
  rusage.set_ipc_messages_received(s_rusage.ru_msgrcv);
  rusage.set_signals_received(s_rusage.ru_nsignals);
  rusage.set_voluntary_context_switches(s_rusage.ru_nvcsw);
  rusage.set_involuntary_context_switches(s_rusage.ru_nivcsw);

  return rusage;
}

RUsage DiffStructRUsageToMessage(const struct rusage &start,
                                 const struct rusage &end) {
  RUsage rusage;

  rusage.set_user_cpu_time(TimevalToDouble(end.ru_utime) -
                           TimevalToDouble(start.ru_utime));
  rusage.set_system_cpu_time(TimevalToDouble(end.ru_stime) -
                             TimevalToDouble(start.ru_stime));
  rusage.set_max_resident_set_size(end.ru_maxrss - start.ru_maxrss);
  rusage.set_integral_shared_memory_size(end.ru_ixrss - start.ru_ixrss);
  rusage.set_integral_unshared_data_size(end.ru_idrss - start.ru_idrss);
  rusage.set_integral_unshared_stack_size(end.ru_isrss - start.ru_isrss);
  rusage.set_page_reclaims_soft_page_faults(end.ru_minflt - start.ru_minflt);
  rusage.set_page_faults_hard_page_faults(end.ru_majflt - start.ru_majflt);
  rusage.set_swaps(end.ru_nswap - start.ru_nswap);
  rusage.set_block_input_operations(end.ru_inblock - start.ru_inblock);
  rusage.set_block_output_operations(end.ru_oublock - start.ru_oublock);
  rusage.set_ipc_messages_sent(end.ru_msgsnd - start.ru_msgsnd);
  rusage.set_ipc_messages_received(end.ru_msgrcv - start.ru_msgrcv);
  rusage.set_signals_received(end.ru_nsignals - start.ru_nsignals);
  rusage.set_voluntary_context_switches(end.ru_nvcsw - start.ru_nvcsw);
  rusage.set_involuntary_context_switches(end.ru_nivcsw - start.ru_nivcsw);

  return rusage;
}

RUsageStats GetRUsageStatsFromStructs(const struct rusage &start,
                                      const struct rusage &end) {
  RUsage *rusage_start = new RUsage();
  RUsage *rusage_diff = new RUsage();
  *rusage_start = StructRUsageToMessage(start);
  *rusage_diff = DiffStructRUsageToMessage(start, end);
  RUsageStats rusage_stats;
  rusage_stats.set_allocated_rusage_start(rusage_start);
  rusage_stats.set_allocated_rusage_diff(rusage_diff);
  return rusage_stats;
}

struct rusage DoGetRusage() {
  struct rusage rusage;
  int ret = getrusage(RUSAGE_SELF, &rusage);
  if (ret != 0)
    LOG(WARNING) << "getrusage failed !";
  return rusage;
}

}  // namespace distbench
