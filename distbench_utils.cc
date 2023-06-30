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

#include "distbench_utils.h"

#include <fcntl.h>
#include <sys/resource.h>

#include <cerrno>
#include <fstream>
#include <streambuf>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

namespace std {
ostream& operator<<(ostream& out, grpc::Status const& c) {
  return out << "(grpc::status: " << c.error_message() << ")";
}
}  // namespace std

namespace distbench {

std::string Hostname() {
  char hostname[4096] = {};
  if (gethostname(hostname, sizeof(hostname)) != 0) {
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

void InitLibs(const char* argv0) {
  // Extra library initialization can go here
  ::google::InitGoogleLogging(argv0);
  GOOGLE_PROTOBUF_VERIFY_VERSION;
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
      std::string instance = GetServiceInstanceName(service, i);
      // LOG(INFO) << "service " << instance << " = " << ret.size();
      ret[instance] = ret.size();
    }
  }
  return ret;
}

absl::StatusOr<ServiceSpec> GetServiceSpec(
    std::string_view name, const DistributedSystemDescription& config) {
  for (const auto& service : config.services()) {
    if (service.name() == name) {
      return service;
    }
  }
  return absl::NotFoundError(absl::StrCat("Service '", name, "' not found"));
}

grpc::Status Annotate(const grpc::Status& status, std::string_view context) {
  return grpc::Status(status.error_code(),
                      absl::StrCat(context, status.error_message()));
}

grpc::Status abslStatusToGrpcStatus(const absl::Status& status) {
  if (status.ok()) return grpc::Status::OK;

  std::string message = std::string(status.message());
  // GRPC and ABSL (currently) share the same error codes
  grpc::StatusCode code = (grpc::StatusCode)status.code();
  return grpc::Status(code, message);
}

absl::Status grpcStatusToAbslStatus(const grpc::Status& status) {
  if (status.ok()) return absl::OkStatus();

  std::string message = status.error_message();
  // GRPC and ABSL (currently) share the same error codes
  absl::StatusCode code = (absl::StatusCode)status.error_code();
  return absl::Status(code, message);
}

void SetGrpcClientContextDeadline(grpc::ClientContext* context,
                                  int max_time_s) {
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(max_time_s);
  context->set_deadline(deadline);
}

absl::StatusOr<std::string> ReadFileToString(const std::string& filename) {
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

void ApplyServerSettingsToGrpcBuilder(grpc::ServerBuilder* builder,
                                      const ProtocolDriverOptions& pd_opts) {
  for (const auto& setting : pd_opts.server_settings()) {
    if (!setting.has_name()) {
      LOG(ERROR) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto& name = setting.name();
    if (setting.has_string_value()) {
      builder->AddChannelArgument(name, setting.string_value());
      continue;
    }
    if (setting.has_int64_value()) {
      builder->AddChannelArgument(name, setting.int64_value());
      continue;
    }

    LOG(ERROR) << "ProtocolDriverOptions.NamedSetting[" << name << "]"
               << " no setting found (str or int)!";
  }
}

// RUsage functions
namespace {
double TimevalToDouble(const struct timeval& t) {
  return (double)t.tv_usec / 1'000'000.0 + t.tv_sec;
}
}  // Anonymous namespace

RUsage StructRUsageToMessage(const struct rusage& s_rusage) {
  RUsage rusage;

  rusage.set_user_cpu_time_seconds(TimevalToDouble(s_rusage.ru_utime));
  rusage.set_system_cpu_time_seconds(TimevalToDouble(s_rusage.ru_stime));
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

RUsage DiffStructRUsageToMessage(const struct rusage& start,
                                 const struct rusage& end) {
  RUsage rusage;

  rusage.set_user_cpu_time_seconds(TimevalToDouble(end.ru_utime) -
                                   TimevalToDouble(start.ru_utime));
  rusage.set_system_cpu_time_seconds(TimevalToDouble(end.ru_stime) -
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

RUsageStats GetRUsageStatsFromStructs(const struct rusage& start,
                                      const struct rusage& end) {
  RUsage* rusage_start = new RUsage();
  RUsage* rusage_diff = new RUsage();
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
  if (ret != 0) {
    LOG(WARNING) << "getrusage failed !";
  }
  return rusage;
}

std::string GetNamedSettingString(
    const ::google::protobuf::RepeatedPtrField<distbench::NamedSetting>&
        settings,
    absl::string_view setting_name, std::string default_value) {
  for (const auto& setting : settings) {
    if (!setting.has_name()) {
      LOG(ERROR) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto& name = setting.name();
    if (name != setting_name) continue;
    if (setting.has_int64_value()) {
      LOG(ERROR) << "ProtocolDriverOptions.NamedSetting[" << name
                 << "] should be a string !";
      continue;
    }
    if (setting.has_string_value()) {
      return setting.string_value();
    }
  }

  return default_value;
}

std::string GetNamedServerSettingString(
    const distbench::ProtocolDriverOptions& opts, absl::string_view name,
    std::string default_value) {
  return GetNamedSettingString(opts.server_settings(), name, default_value);
}

std::string GetNamedClientSettingString(
    const distbench::ProtocolDriverOptions& opts, absl::string_view name,
    std::string default_value) {
  return GetNamedSettingString(opts.client_settings(), name, default_value);
}

int64_t GetNamedSettingInt64(
    const ::google::protobuf::RepeatedPtrField<distbench::NamedSetting>&
        settings,
    absl::string_view setting_name, int64_t default_value) {
  for (const auto& setting : settings) {
    if (!setting.has_name()) {
      LOG(ERROR) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto& name = setting.name();
    if (name != setting_name) continue;
    if (setting.has_string_value()) {
      LOG(ERROR) << "ProtocolDriverOptions.NamedSetting[" << name
                 << "] should be an int !";
      continue;
    }
    if (setting.has_int64_value()) {
      return setting.int64_value();
    }
  }

  return default_value;
}

int64_t GetNamedServerSettingInt64(const distbench::ProtocolDriverOptions& opts,
                                   absl::string_view name,
                                   int64_t default_value) {
  return GetNamedSettingInt64(opts.server_settings(), name, default_value);
}

int64_t GetNamedClientSettingInt64(const distbench::ProtocolDriverOptions& opts,
                                   absl::string_view name,
                                   int64_t default_value) {
  return GetNamedSettingInt64(opts.client_settings(), name, default_value);
}

absl::StatusOr<int64_t> GetNamedAttributeInt64(
    const distbench::DistributedSystemDescription& test, absl::string_view name,
    int64_t default_value) {
  auto attributes = test.attributes();
  auto it = attributes.find(name);
  if (it == attributes.end()) {
    return default_value;
  }
  int64_t value;
  bool success = absl::SimpleAtoi(it->second, &value);
  if (success) {
    return value;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot convert test attribute ", name, " value (",
                     it->second, ") to int."));
  }
}

// Parse/Read TestSequence protos.
absl::StatusOr<TestSequence> ParseTestSequenceTextProto(
    const std::string& text_proto) {
  TestSequence test_sequence;
  if (::google::protobuf::TextFormat::ParseFromString(text_proto,
                                                      &test_sequence)) {
    return test_sequence;
  }
  return absl::InvalidArgumentError("Error parsing the TestSequence proto");
}

absl::StatusOr<TestSequence> ParseTestSequenceProtoFromFile(
    const std::string& filename) {
  absl::StatusOr<std::string> proto_string = ReadFileToString(filename);
  if (!proto_string.ok()) return proto_string.status();

  // Attempt to parse, assuming it is binary.
  TestSequence test_sequence;
  if (test_sequence.ParseFromString(*proto_string)) return test_sequence;

  // Attempt to parse, assuming it is text.
  auto result = ParseTestSequenceTextProto(*proto_string);
  if (result.ok()) return result;

  return absl::InvalidArgumentError(
      "Error parsing the TestSequence proto file (both in binary and text "
      "modes");
}

// Write TestSequenceResults protos.
absl::Status SaveResultProtoToFile(
    const std::string& filename, const distbench::TestSequenceResults& result) {
  int fd_proto = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd_proto < 0) {
    std::string error_message{
        "Error opening the output result proto file for writing: "};
    return absl::InvalidArgumentError(error_message + filename);
  }

  ::google::protobuf::io::FileOutputStream fos_resultproto(fd_proto);
  if (!::google::protobuf::TextFormat::Print(result, &fos_resultproto)) {
    return absl::InvalidArgumentError("Error writing the result proto file");
  }

  return absl::OkStatus();
}

absl::Status SaveResultProtoToFileBinary(
    const std::string& filename, const distbench::TestSequenceResults& result) {
  std::fstream output(filename,
                      std::ios::out | std::ios::trunc | std::ios::binary);
  if (!result.SerializeToOstream(&output)) {
    return absl::InvalidArgumentError(
        "Error writing the result proto file in binary mode");
  }

  return absl::OkStatus();
}

void AddServerInt64OptionTo(ProtocolDriverOptions& pdo, std::string option_name,
                            int64_t value) {
  auto* ns = pdo.add_server_settings();
  ns->set_name(option_name);
  ns->set_int64_value(value);
}

void AddServerStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value) {
  auto* ns = pdo.add_server_settings();
  ns->set_name(option_name);
  ns->set_string_value(value);
}

void AddClientStringOptionTo(ProtocolDriverOptions& pdo,
                             std::string option_name, std::string value) {
  auto* ns = pdo.add_client_settings();
  ns->set_name(option_name);
  ns->set_string_value(value);
}

void AddActivitySettingIntTo(ActivityConfig* ac, std::string option_name,
                             int value) {
  auto* ns = ac->add_activity_settings();
  ns->set_name(option_name);
  ns->set_int64_value(value);
}

void AddActivitySettingStringTo(ActivityConfig* ac, std::string option_name,
                                std::string value) {
  auto* ns = ac->add_activity_settings();
  ns->set_name(option_name);
  ns->set_string_value(value);
}

namespace {

absl::Status ValidatePmfConfig(const DistributionConfig& config) {
  float cdf = 0;
  int dimensions = config.field_names_size();
  if (dimensions == 0) {
    dimensions = config.pmf_points(0).data_points_size();
  }
  for (const auto& point : config.pmf_points()) {
    if (point.data_points_size() != dimensions) {
      if (config.field_names_size()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "The number of data_points must match the number of field_names."));
      } else {
        return absl::InvalidArgumentError(absl::StrCat(
            "The number of data_points must be the same in all PmfPoints."));
      }
    }
    cdf += point.pmf();
  }
  if (fabs(cdf - 1.0) > 1e-6) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The cumulative value of all PMFs ", cdf, " should be 1.0 +-1e-6)."));
  }
  return absl::OkStatus();
};

absl::Status ValidateCdfConfig(const DistributionConfig& config) {
  auto prev_cdf = config.cdf_points(0).cdf();
  if (prev_cdf < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("The cdf value:'", prev_cdf,
                     "' must not be negative in CDF:'", config.name(), "'."));
  }

  auto prev_value = config.cdf_points(0).value();
  for (int i = 1; i < config.cdf_points_size(); i++) {
    auto current_value = config.cdf_points(i).value();
    auto current_cdf = config.cdf_points(i).cdf();
    if (current_value <= prev_value) {
      return absl::InvalidArgumentError(absl::StrCat(
          "The value:'", current_value, "' must be greater than previous_value:'",
          prev_value, "' at index '", i, "' in CDF:'", config.name(), "'."));
    }
    if (current_cdf < prev_cdf) {
      return absl::InvalidArgumentError(
          absl::StrCat("The cdf value:'", current_cdf,
                       "' must be greater than previous cdf value:'", prev_cdf,
                       "' at index '", i, "' in CDF:'", config.name(), "'."));
    }
    prev_value = current_value;
    prev_cdf = current_cdf;
  }

  auto last_configured_cdf =
      config.cdf_points(config.cdf_points_size() - 1).cdf();
  if (last_configured_cdf != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The maximum value of cdf is '", last_configured_cdf, "' in CDF:'",
        config.name(), "'. It must be exactly equal to 1."));
  }
  return absl::OkStatus();
};

}  // anonymous namespace

absl::Status ValidateDistributionConfig(const DistributionConfig& config) {
  auto cdf_present = config.cdf_points_size() != 0;
  auto pmf_present = config.pmf_points_size() != 0;

  if (cdf_present == pmf_present) {
    return absl::InvalidArgumentError(
        absl::StrCat("Exactly one of CDF and PMF must be provided for '",
                     config.name(), "'."));
  }

  std::set<std::string> field_names;
  for (const auto& name : config.field_names()) {
    if (field_names.count(name)) {
      return absl::InvalidArgumentError(
          absl::StrCat("field_name '", name, "'repeats in ", config.name()));
    }
    field_names.insert(name);
  }

  if (cdf_present) return ValidateCdfConfig(config);
  if (pmf_present) return ValidatePmfConfig(config);

  return absl::InvalidArgumentError("We cannot get here.");
};

// Get the canonical version of DistributionConfig from the config
// provided by the user. The canonical version of the config has
// the fields rearranged as per 'kFieldNames' and has exactly
// 'kMaxFieldNames' dimensions. This avoids iterating through
// field_names for every sample generated by the sample
// generator. If input_config has CDF points, they will be converted into
// PMF points and added to the canonical version of Distribution Config.
absl::StatusOr<DistributionConfig> GetCanonicalDistributionConfig(
    const DistributionConfig& input_config, const char* canonical_fields[]) {
  std::vector<std::string_view> fields;
  while (*canonical_fields) {
    fields.push_back(*canonical_fields);
    ++canonical_fields;
  }
  return GetCanonicalDistributionConfig(input_config, fields);
}

absl::StatusOr<DistributionConfig> GetCanonicalDistributionConfig(
    const DistributionConfig& input_config,
    std::vector<std::string_view> canonical_fields) {
  auto status = ValidateDistributionConfig(input_config);
  if (!status.ok()) return status;

  DistributionConfig canonical_config;
  canonical_config.set_name(input_config.name());
  std::vector<int> canonical_from_proto(canonical_fields.size(), -1);

  for (size_t i = 0; i < canonical_fields.size(); ++i) {
    canonical_config.add_field_names(std::string(canonical_fields[i]));
    for (int j = 0; j < input_config.field_names_size(); j++) {
      if (input_config.field_names(j) == canonical_fields[i]) {
        canonical_from_proto[i] = j;
      }
    }

    // Try to parse field_name as an int. If sucessful it's a fixed value.
    if (canonical_from_proto[i] == -1) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Field missing from config: '", canonical_fields[i], "'."));
    }
  }

  for (int i = 0; i < input_config.pmf_points_size(); i++) {
    auto* output_pmf_point = canonical_config.add_pmf_points();
    auto input_pmf_point = input_config.pmf_points(i);
    output_pmf_point->set_pmf(input_pmf_point.pmf());

    for (size_t j = 0; j < canonical_fields.size(); j++) {
      auto& input_pmf_point = input_config.pmf_points(i);
      auto* output_data_point = output_pmf_point->add_data_points();
      output_data_point->CopyFrom(
          input_pmf_point.data_points(canonical_from_proto[j]));
    }
  }

  double prev_cdf = 0.0;
  // If the distribution is uniform, then make pmf points with datapoints that
  // are intervals, else make pmf points with datapoints that have exact values.
  if (input_config.cdf_points_size() && input_config.cdf_points(0).cdf() == 0) {
    int64_t next_lower_bound = input_config.cdf_points(0).value();
    for (int i = 1; i < input_config.cdf_points_size(); i++) {
      auto* output_pmf_point = canonical_config.add_pmf_points();
      auto input_cdf_point = input_config.cdf_points(i);
      output_pmf_point->set_pmf(input_cdf_point.cdf() - prev_cdf);
      prev_cdf = input_cdf_point.cdf();
      for (size_t j = 0; j < canonical_fields.size(); j++) {
        auto* output_data_point = output_pmf_point->add_data_points();
        output_data_point->set_lower(next_lower_bound);
        output_data_point->set_upper(input_cdf_point.value());
      }
      next_lower_bound = input_cdf_point.value() + 1;
    }
  } else {
    for (int i = 0; i < input_config.cdf_points_size(); i++) {
      auto* output_pmf_point = canonical_config.add_pmf_points();
      auto input_cdf_point = input_config.cdf_points(i);
      output_pmf_point->set_pmf(input_cdf_point.cdf() - prev_cdf);
      prev_cdf = input_cdf_point.cdf();
      for (size_t j = 0; j < canonical_fields.size(); j++) {
        auto* output_data_point = output_pmf_point->add_data_points();
        output_data_point->set_exact(input_cdf_point.value());
      }
    }
  }

  return canonical_config;
}

absl::StatusOr<ServiceSpec> GetCanonicalServiceSpec(
    const ServiceSpec& service_spec) {
  int xyz_size = 1;
  if (service_spec.has_x_size()) {
    if (service_spec.x_size() <= 0) {
      return absl::InvalidArgumentError("x_size must be positive");
    }
    xyz_size *= service_spec.x_size();
    if (service_spec.has_y_size()) {
      if (service_spec.y_size() <= 0) {
        return absl::InvalidArgumentError("y_size must be positive");
      }
      xyz_size *= service_spec.y_size();
      if (service_spec.has_z_size()) {
        if (service_spec.z_size() <= 0) {
          return absl::InvalidArgumentError("z_size must be positive");
        }
        xyz_size *= service_spec.z_size();
      }
    } else {
      if (service_spec.has_z_size()) {
        return absl::InvalidArgumentError(
            "z_size cannot be specified without y_size.");
      }
    }
  } else {
    if (service_spec.has_y_size()) {
      return absl::InvalidArgumentError(
          "y_size cannot be specified without x_size.");
    }
    if (service_spec.has_z_size()) {
      return absl::InvalidArgumentError(
          "z_size cannot be specified without x_size and y_size.");
    }
  }
  if (service_spec.has_count() && service_spec.has_x_size()
      && service_spec.count() != xyz_size) {
    return absl::InvalidArgumentError("count does not match x/y/z size");
  }
  ServiceSpec ret = service_spec;
  ret.set_count(xyz_size);
  return ret;
}

InstanceRanks GetServiceInstanceRanksFromName(std::string_view name) {
  std::string_view name_view = name;
  size_t prefix_length = name_view.find_first_of("(");
  if (prefix_length != std::string::npos) {
    name_view.remove_prefix(prefix_length + 1);
  }
  prefix_length = name_view.find_first_of("/");
  if (prefix_length != std::string::npos) {
    name_view.remove_prefix(prefix_length + 1);
  }
  InstanceRanks ret = {0, 0, 0};
  if (!sscanf(name_view.data(), "%d,%d,%d", &ret.x, &ret.y, &ret.z)) {
    LOG(INFO) << name_view;
    LOG(FATAL) << "could not read ranks from " << name;
  }
  return ret;
}

int GetLinearServiceInstanceFromRanks(const distbench::ServiceSpec& service_spec, InstanceRanks ranks) {
  int instance = ranks.x +
                 ranks.y * service_spec.x_size() +
                 ranks.z * service_spec.x_size() * service_spec.y_size();
  return instance;
}

InstanceRanks GetServiceInstanceRanks(const ServiceSpec& service_spec,
                                      int instance) {
  InstanceRanks ranks = {instance, 0, 0};
  if (service_spec.has_x_size()) {
    ranks.x = instance % service_spec.x_size();
    if (service_spec.has_y_size()) {
      ranks.y = (instance / service_spec.x_size()) % service_spec.y_size();
      if (service_spec.has_z_size()) {
        ranks.z = (instance / service_spec.x_size()) / service_spec.y_size();
      }
    }
  }
  return ranks;
}

std::string GetServiceInstanceName(const ServiceSpec& service_spec,
                                   int instance) {
  if (!service_spec.has_x_size()) {
    return absl::StrCat(service_spec.name(), "/", instance);
  }
  InstanceRanks ranks = GetServiceInstanceRanks(service_spec, instance);
  if (service_spec.has_z_size()) {
    return absl::StrCat(
        service_spec.name(), "/(", ranks.x, ",", ranks.y, ",", ranks.z, ")");
  }
  if (service_spec.has_y_size()) {
    return absl::StrCat(service_spec.name(), "/(", ranks.x, ",", ranks.y, ")");
  }
  return absl::StrCat(service_spec.name(), "/(", ranks.x, ")");
}

absl::StatusOr<DistributedSystemDescription>
GetCanonicalDistributedSystemDescription(
    const DistributedSystemDescription& traffic_config) {
  DistributedSystemDescription ret = traffic_config;
  ret.clear_services();
  for (const auto& service : traffic_config.services()) {
    auto maybe_service = GetCanonicalServiceSpec(service);
    if (!maybe_service.ok()) {
      return maybe_service.status();
    }
    *ret.add_services() = maybe_service.value();
  }
  return traffic_config;
}


absl::StatusOr<TestSequence> GetCanonicalTestSequence(
    const TestSequence& sequence) {
  TestSequence ret;
  *ret.mutable_tests_setting() = sequence.tests_setting();
  for (const auto& test : sequence.tests()) {
    auto maybe_test = GetCanonicalDistributedSystemDescription(test);
    if (!maybe_test.ok()) {
      return maybe_test.status();
    }
    *ret.add_tests() = maybe_test.value();
  }
  return ret;
}

}  // namespace distbench
