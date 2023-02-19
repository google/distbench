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

#include "distbench_summary.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"

namespace distbench {

namespace {

std::string LatencySummary(std::vector<int64_t> latencies) {
  size_t N = latencies.size();
  std::string ret;
  absl::StrAppendFormat(&ret, "N: %ld", N);
  if (N > 0) {
    absl::StrAppendFormat(&ret, " min: %ldns", *latencies.begin());
    absl::StrAppendFormat(&ret, " median: %ldns", latencies[N * 0.5]);
    absl::StrAppendFormat(&ret, " 90%%: %ldns", latencies[N * 0.9]);
    absl::StrAppendFormat(&ret, " 99%%: %ldns", latencies[N * 0.99]);
    absl::StrAppendFormat(&ret, " 99.9%%: %ldns", latencies[N * 0.999]);
    absl::StrAppendFormat(&ret, " max: %ldns", *latencies.rbegin());
  }
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

void AddCommunicationSummaryTo(
    std::vector<std::string>& ret, double total_time_seconds,
    std::map<t_string_pair, rpc_traffic_summary> perf_map) {
  ret.push_back("Communication summary:");
  constexpr double MiB = 1024 * 1024;
  for (auto& perf_r : perf_map) {
    std::string name = perf_r.first.first + " -> " + perf_r.first.second;
    auto& perf = perf_r.second;
    std::string str{};
    absl::StrAppendFormat(
        &str,
        "  %s: RPCs: %d (%3.2f kQPS) "
        "Request: %3.1f MiB/s Response: %3.1f MiB/s",
        name, perf.nb_rpcs, (double)perf.nb_rpcs / total_time_seconds / 1000.,
        (double)perf.request_size / MiB / total_time_seconds,
        (double)perf.response_size / MiB / total_time_seconds);
    ret.push_back(str);
  }
}

void AddInstanceSummaryTo(std::vector<std::string>& ret,
                          double total_time_seconds,
                          std::map<t_string_pair, rpc_traffic_summary> perf_map,
                          int64_t nb_warmup_samples,
                          int64_t nb_failed_samples) {
  std::map<std::string, instance_summary> instance_summary_map;
  int64_t total_rpcs = 0;
  int64_t total_tx_bytes = 0;
  for (auto& perf_r : perf_map) {
    std::string initiator_name = perf_r.first.first;
    std::string target_name = perf_r.first.second;
    auto& perf = perf_r.second;
    instance_summary inst_summary{};
    total_rpcs += perf.nb_rpcs;
    total_tx_bytes += perf.request_size + perf.response_size;

    auto is = instance_summary_map.find(initiator_name);
    if (is != instance_summary_map.end()) {
      inst_summary = is->second;
    }

    inst_summary.tx_payload_bytes += perf.request_size;
    inst_summary.rx_payload_bytes += perf.response_size;
    instance_summary_map[initiator_name] = inst_summary;

    inst_summary = instance_summary{};
    auto ist = instance_summary_map.find(target_name);
    if (ist != instance_summary_map.end()) {
      inst_summary = ist->second;
    }

    inst_summary.tx_payload_bytes += perf.response_size;
    inst_summary.rx_payload_bytes += perf.request_size;
    instance_summary_map[target_name] = inst_summary;
  }

  ret.push_back("Instance summary:");
  constexpr int64_t MiB = 1024 * 1024;
  for (auto& instance_sum : instance_summary_map) {
    instance_summary inst_summary = instance_sum.second;
    std::string str{};
    absl::StrAppendFormat(
        &str, "  %s: Tx: %3.1f MiB/s, Rx:%3.1f MiB/s", instance_sum.first,
        (double)inst_summary.tx_payload_bytes / MiB / total_time_seconds,
        (double)inst_summary.rx_payload_bytes / MiB / total_time_seconds);
    ret.push_back(str);
  }

  std::string str{};
  ret.push_back("Global summary:");
  ret.push_back(absl::StrCat("  Failed samples: ", nb_failed_samples));
  ret.push_back(absl::StrCat("  Warmup samples: ", nb_warmup_samples));
  absl::StrAppendFormat(&str, "  Total time: %3.3fs", total_time_seconds);
  ret.push_back(str);
  str = "";
  absl::StrAppendFormat(&str, "  Total Tx: %d MiB (%3.1f MiB/s), ",
                        total_tx_bytes / MiB,
                        (double)total_tx_bytes / MiB / total_time_seconds);
  absl::StrAppendFormat(&str, "Total Nb RPCs: %d (%3.2f kQPS)", total_rpcs,
                        (double)total_rpcs / 1000 / total_time_seconds);
  ret.push_back(str);
}

}  // anonymous namespace

std::vector<std::string> SummarizeTestResult(const TestResult& test_result) {
  std::map<std::string, std::vector<int64_t>> latency_map;
  std::map<t_string_pair, rpc_traffic_summary> perf_map;
  int64_t test_time = 0;
  int64_t nb_warmup_samples = 0;
  int64_t nb_failed_samples = 0;

  for (const auto& instance_log : test_result.service_logs().instance_logs()) {
    const std::string& initiator_instance_name = instance_log.first;
    for (const auto& peer_log : instance_log.second.peer_logs()) {
      const std::string& target_instance_name = peer_log.first;
      int64_t start_timestamp_ns = std::numeric_limits<int64_t>::max();
      int64_t end_timestamp_ns = std::numeric_limits<int64_t>::min();
      rpc_traffic_summary perf_record{};
      for (const auto& rpc_log : peer_log.second.rpc_logs()) {
        std::string rpc_name =
            test_result.traffic_config().rpc_descriptions(rpc_log.first).name();
        std::vector<int64_t>& latencies = latency_map[rpc_name];
        perf_record.nb_rpcs += rpc_log.second.successful_rpc_samples().size();
        nb_failed_samples += rpc_log.second.failed_rpc_samples().size();
        for (const auto& sample : rpc_log.second.successful_rpc_samples()) {
          int64_t rpc_start_timestamp_ns = sample.start_timestamp_ns();
          int64_t rpc_latency_ns = sample.latency_ns();
          int64_t rpc_request_size = sample.request_size();
          int64_t rpc_response_size = sample.response_size();
          if (sample.warmup()) {
            ++nb_warmup_samples;
            continue;
          }

          start_timestamp_ns =
              std::min(rpc_start_timestamp_ns, start_timestamp_ns);
          end_timestamp_ns = std::max(rpc_start_timestamp_ns + rpc_latency_ns,
                                      end_timestamp_ns);
          perf_record.request_size += rpc_request_size;
          perf_record.response_size += rpc_response_size;
          latencies.push_back(rpc_latency_ns);
        }
      }
      if (start_timestamp_ns != std::numeric_limits<int64_t>::max()) {
        test_time = std::max(test_time, end_timestamp_ns - start_timestamp_ns);
      }
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
    absl::StrAppendFormat(&str, "  %s: %s", latencies.first,
                          LatencySummary(latencies.second));
    ret.push_back(str);
  }

  double total_time_seconds = (double)test_time / 1'000'000'000;
  AddCommunicationSummaryTo(ret, total_time_seconds, perf_map);
  AddInstanceSummaryTo(ret, total_time_seconds, perf_map, nb_warmup_samples,
                       nb_failed_samples);
  return ret;
}

}  // namespace distbench
