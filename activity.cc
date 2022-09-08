// Copyright 2022 Google LLC
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

#include "activity.h"

#include "glog/logging.h"

namespace distbench {

absl::StatusOr<std::unique_ptr<Activity>> AllocateActivity(
    const ActivityConfig& ac) {
  std::unique_ptr<Activity> activity;
  auto activity_func =
      GetNamedSettingString(ac.activity_settings(), "activity_func", "None");
  if (activity_func == "WasteCpu") {
    activity = std::make_unique<WasteCpu>();
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid activity '", activity_func, "' was requested."));
  }
  return activity;
}

#pragma GCC push_options
#pragma GCC optimize("O0")
void WasteCpu::DoActivity() {
  iteration_count_++;
  int sum = 0;
  std::vector<int> v(array_size_, 0);
  srand(time(0));
  generate(v.begin(), v.end(), rand);
  std::sort(v.begin(), v.end());
  for (auto num : v) sum += num;
}
#pragma GCC pop_options

ActivityLog WasteCpu::GetActivityLog() {
  ActivityLog alog;
  if (iteration_count_) {
    auto* am = alog.add_activity_metrics();
    am->set_name("iteration_count");
    am->set_value_int(iteration_count_);
  }
  return alog;
}

absl::Status WasteCpu::Initialize(const ActivityConfig& ac) {
  array_size_ =
      GetNamedSettingInt64(ac.activity_settings(), "array_size", 1000);
  activity_name_ = ac.name();
  iteration_count_ = 0;
  return absl::OkStatus();
}

}  // namespace distbench
