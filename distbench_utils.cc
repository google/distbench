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

#include <cerrno>
#include <fstream>
#include <streambuf>

#include "interface_lookup.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "glog/logging.h"

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
  net_base::IPAddress ip;
  if (use_ipv4_first) {
    CHECK(net_base::InterfaceLookup::MyIPv4Address(&ip) ||
          net_base::InterfaceLookup::MyIPv6Address(&ip));
  } else {
    CHECK(net_base::InterfaceLookup::MyIPv6Address(&ip) ||
          net_base::InterfaceLookup::MyIPv4Address(&ip));
  }
  return ip.ToString();
}

std::string SocketAddressForDevice(std::string_view netdev, int port) {
  net_base::IPAddress ip;

  if (use_ipv4_first &&
      net_base::InterfaceLookup::MyIPv4Address(&ip))
    return absl::StrCat(ip.ToString(), ":", port);

  if (net_base::InterfaceLookup::MyIPv6Address(&ip))
    return absl::StrCat("[", ip.ToString(), "]:", port);

  if (net_base::InterfaceLookup::MyIPv4Address(&ip))
    return absl::StrCat(ip.ToString(), ":", port);

  LOG(FATAL) << "Could not get ip v4/v6 address";
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

namespace {

std::string LatencySummary(std::vector<int64_t> latencies) {
  std::string ret;
  CHECK(!latencies.empty());
  size_t N =  latencies.size();
  absl::StrAppendFormat(&ret, "N: %ld", N);
  absl::StrAppendFormat(&ret, " min: %ldns", *latencies.begin());
  absl::StrAppendFormat(&ret, " median: %ldns", latencies[N * 0.5]);
  absl::StrAppendFormat(&ret, " 90%%: %ldns", latencies[N * 0.9]);
  absl::StrAppendFormat(&ret, " 99%%: %ldns", latencies[N * 0.99]);
  absl::StrAppendFormat(&ret, " 99.9%%: %ldns", latencies[N * 0.999]);
  absl::StrAppendFormat(&ret, " max: %ldns", *latencies.rbegin());
  return ret;
}

struct rpc_traffic_summary {
  int64_t nb_rpcs = 0;
  int64_t request_size = 0;
  int64_t response_size = 0;
};

typedef std::pair<std::string, std::string> t_string_pair;

struct instance_summary {
  int64_t tx_payload_bytes = 0;
  int64_t rx_payload_bytes = 0;
};

void AddCommunicationSummaryTo(std::vector<std::string> &ret,
    double total_time_seconds,
    std::map<t_string_pair, rpc_traffic_summary> perf_map) {
  ret.push_back("Communication summary:");
  constexpr double MiB = 1024 * 1024;
  for (auto& perf_r : perf_map) {
    std::string name = perf_r.first.first + " -> " + perf_r.first.second;
    auto& perf = perf_r.second;
    std::string str{};
    absl::StrAppendFormat(&str,
        "  %s: RPCs: %d (%3.2f kQPS) "
        "Request: %3.1f MiB/s Response: %3.1f MiB/s",
        name, perf.nb_rpcs, (double)perf.nb_rpcs / total_time_seconds / 1000.,
        (double)perf.request_size / MiB / total_time_seconds,
        (double)perf.response_size / MiB / total_time_seconds);
    ret.push_back(str);
  }
}

void AddInstanceSummaryTo(std::vector<std::string> &ret,
    double total_time_seconds,
    std::map<t_string_pair, rpc_traffic_summary> perf_map) {
  std::map<std::string, instance_summary> instance_summary_map;
  int64_t total_rpcs = 0;
  int64_t total_tx_bytes = 0;
  for (auto& perf_r : perf_map) {
    std::string initiator_name = perf_r.first.first;
    std::string target_name = perf_r.first.second;
    auto &perf = perf_r.second;
    instance_summary inst_summary{};
    total_rpcs += perf.nb_rpcs;
    total_tx_bytes += perf.request_size + perf.response_size;

    auto is = instance_summary_map.find(initiator_name);
    if (is != instance_summary_map.end())
      inst_summary = is->second;

    inst_summary.tx_payload_bytes += perf.request_size;
    inst_summary.rx_payload_bytes += perf.response_size;
    instance_summary_map[initiator_name] = inst_summary;

    inst_summary = instance_summary{};
    auto ist = instance_summary_map.find(target_name);
    if (ist != instance_summary_map.end())
      inst_summary = ist->second;

    inst_summary.tx_payload_bytes += perf.response_size;
    inst_summary.rx_payload_bytes += perf.request_size;
    instance_summary_map[target_name] = inst_summary;
  }

  ret.push_back("Instance summary:");
  constexpr int64_t MiB = 1024 * 1024;
  for (auto& instance_sum: instance_summary_map) {
    instance_summary inst_summary = instance_sum.second;
    std::string str{};
    absl::StrAppendFormat(&str, "  %s: Tx: %3.1f MiB/s, Rx:%3.1f MiB/s",
        instance_sum.first,
        (double)inst_summary.tx_payload_bytes / MiB / total_time_seconds,
        (double)inst_summary.rx_payload_bytes / MiB / total_time_seconds);
    ret.push_back(str);
  }

  std::string str{};
  ret.push_back("Global summary:");
  absl::StrAppendFormat(&str, "  Total time: %3.3fs", total_time_seconds);
  ret.push_back(str);
  str = "";
  absl::StrAppendFormat(&str, "  Total Tx: %d MiB (%3.1f MiB/s), ",
      total_tx_bytes / MiB,
      (double)total_tx_bytes / MiB / total_time_seconds);
  absl::StrAppendFormat(&str,
      "Total Nb RPCs: %d (%3.2f kQPS)",
      total_rpcs, (double)total_rpcs / 1000 / total_time_seconds);
  ret.push_back(str);
}

}  // anonymous namespace

std::vector<std::string> SummarizeTestResult(const TestResult& test_result) {
  std::map<std::string, std::vector<int64_t>> latency_map;
  std::map<t_string_pair, rpc_traffic_summary> perf_map;
  int64_t test_time = 0;

  for (const auto& instance_log : test_result.service_logs().instance_logs()) {
    const std::string& initiator_instance_name = instance_log.first;
    for (const auto& peer_log : instance_log.second.peer_logs()) {
      const std::string& target_instance_name = peer_log.first;
      int64_t start_timestamp_ns = std::numeric_limits<int64_t>::max();
      int64_t end_timestamp_ns = std::numeric_limits<int64_t>::min();
      rpc_traffic_summary perf_record{};
      for (const auto& rpc_log : peer_log.second.rpc_logs()) {
        std::string rpc_name  =
          test_result.traffic_config().rpc_descriptions(rpc_log.first).name();
        std::vector<int64_t>& latencies = latency_map[rpc_name];
        perf_record.nb_rpcs += rpc_log.second.successful_rpc_samples().size();
        for (const auto& sample : rpc_log.second.successful_rpc_samples()) {
          int64_t rpc_start_timestamp_ns = sample.start_timestamp_ns();
          int64_t rpc_latency_ns = sample.latency_ns();
          int64_t rpc_request_size = sample.request_size();
          int64_t rpc_response_size = sample.response_size();

          start_timestamp_ns = std::min(rpc_start_timestamp_ns,
                                        start_timestamp_ns);
          end_timestamp_ns = std::max(rpc_start_timestamp_ns + rpc_latency_ns,
                                      end_timestamp_ns);
          perf_record.request_size += rpc_request_size;
          perf_record.response_size += rpc_response_size;
          latencies.push_back(rpc_latency_ns);
        }
      }
      if (start_timestamp_ns != std::numeric_limits<int64_t>::max())
        test_time = std::max(test_time,
                             end_timestamp_ns - start_timestamp_ns);
      t_string_pair key_traffic_sum =
          std::make_pair(initiator_instance_name, target_instance_name);
      perf_map[key_traffic_sum] = perf_record;
    }
  }

  std::vector<std::string> ret;
  ret.push_back("RPC latency summary:");
  for (auto& latencies : latency_map) {
    std::string str{};
    std::sort(latencies.second.begin(), latencies.second.end());
    absl::StrAppendFormat(
        &str, "  %s: %s", latencies.first, LatencySummary(latencies.second));
    ret.push_back(str);
  }

  double total_time_seconds = (double)test_time / 1000 / 1000 / 1000;
  AddCommunicationSummaryTo(ret, total_time_seconds, perf_map);
  AddInstanceSummaryTo(ret, total_time_seconds, perf_map);
  return ret;
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

}  // namespace distbench
