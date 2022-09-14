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

absl::StatusOr<std::unique_ptr<Activity>> AllocateAndInitializeActivity(
    StoredActivityConfig* sac) {
  std::unique_ptr<Activity> activity;
  auto activity_func = sac->activity_func;
  if (activity_func == "WasteCpu") {
    activity = std::make_unique<WasteCpu>();
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid activity '", activity_func, "' was requested."));
  }

  auto status = activity->Initialize(sac);
  if (!status.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failure: Activity Initialize failed for: '", sac->activity_config_name,
        "' with status: ", status.message()));
  }

  return activity;
}

void WasteCpu::DoActivity() {
  iteration_count_++;
  int sum = 0;
  srand(time(0));
  generate(rand_array.begin(), rand_array.end(), rand);
  std::sort(rand_array.begin(), rand_array.end());
  for (auto num : rand_array) sum += num;
  optimization_preventing_num_ = sum;
}

ActivityLog WasteCpu::GetActivityLog() {
  ActivityLog alog;
  if (iteration_count_) {
    auto* am = alog.add_activity_metrics();
    am->set_name("iteration_count");
    am->set_value_int(iteration_count_);
  }
  return alog;
}

absl::Status WasteCpu::Initialize(StoredActivityConfig* sac) {
  auto array_size = sac->waste_cpu_config.array_size;
  if (array_size < 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Array size (", array_size, ") must be a positive integer."));
  }
  rand_array.resize(array_size);
  activity_config_name_ = sac->activity_config_name;
  iteration_count_ = 0;
  return absl::OkStatus();
}

}  // namespace distbench
