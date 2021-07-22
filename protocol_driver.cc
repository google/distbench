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

#include "protocol_driver.h"

#include "glog/logging.h"

namespace distbench {

absl::StatusOr<std::string> ProtocolDriver::Preconnect() {
  return "";
}

void ProtocolDriver::HandleConnectFailure(
    std::string_view local_connection_info) {
}

void ProtocolDriver::ApplyServerSettingsToGrpcBuilder(
    grpc::ServerBuilder &builder,
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
      builder.AddChannelArgument(name, setting.str_value());
      continue;
    }
    if (setting.has_int_value()) {
      LOG(INFO) << "ProtocolDriverOptions.NamedSetting[" << name << "]="
                << setting.int_value();
      builder.AddChannelArgument(name, setting.int_value());
      continue;
    }

    LOG(INFO) << "ProtocolDriverOptions.NamedSetting[" << name << "]"
              << " not setting found (str or int)!";
  }
}

}  // namespace distbench
