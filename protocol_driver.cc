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

class RealClock : public SimpleClock {
 public:
  ~RealClock() override {}
  absl::Time Now() override { return absl::Now(); }

  bool MutexLockWhenWithDeadline(
      absl::Mutex* mu, const absl::Condition& condition, absl::Time deadline)
    ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) override {
    return mu->LockWhenWithDeadline(condition, deadline);
  }
};

SimpleClock& ProtocolDriver::GetClock() {
  static RealClock real_clock;
  return real_clock;
}

absl::StatusOr<std::string> ProtocolDriver::Preconnect() {
  return "";
}

void ProtocolDriver::HandleConnectFailure(
    std::string_view local_connection_info) {
}

int ProtocolDriver::GetNamedSettingInt(
    const distbench::ProtocolDriverOptions &opts,
    absl::string_view setting_name,
    int default_value) {

  for (const auto &setting : opts.server_settings()) {
    if (!setting.has_name()) {
      LOG(ERROR) << "ProtocolDriverOptions NamedSetting has no name !";
      continue;
    }
    const auto &name = setting.name();
    if (name != setting_name)
      continue;
    if (setting.has_str_value()) {
      LOG(ERROR) << "ProtocolDriverOptions.NamedSetting[" << name <<
                "] should be an int !";
      continue;
    }
    if (setting.has_int_value()) {
      int value = setting.int_value();
      return value;
    }
  }

  return default_value;
}

}  // namespace distbench
