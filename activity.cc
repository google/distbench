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

std::unique_ptr<Activity> AllocateActivity(ParsedActivityConfig* config) {
  std::unique_ptr<Activity> activity;
  auto activity_func = config->activity_func;

  if (activity_func == "WasteCpu") {
    activity = std::make_unique<WasteCpu>();
  } else if (activity_func == "PolluteDataCache") {
    activity = std::make_unique<PolluteDataCache>();
  }

  return activity;
}

// Activity: WasteCpu

void WasteCpu::DoActivity() {
  iteration_count_++;
  int sum = 0;
  std::srand(time(0));
  std::generate(rand_array.begin(), rand_array.end(), std::rand);
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

void WasteCpu::Initialize(ParsedActivityConfig* config) {
  rand_array.resize(config->waste_cpu_config.array_size);
  iteration_count_ = 0;
}

absl::Status WasteCpu::ValidateConfig(ActivityConfig& ac) {
  auto array_size =
      GetNamedSettingInt64(ac.activity_settings(), "array_size", 1000);
  if (array_size < 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Array size (", array_size, ") must be a positive integer."));
  }
  return absl::OkStatus();
}

// Activity: PolluteDataCache

absl::Status PolluteDataCache::ValidateConfig(ActivityConfig& ac) {
  auto array_size =
      GetNamedSettingInt64(ac.activity_settings(), "array_size", 1000);
  if (array_size < 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Array size (", array_size, ") must be a positive integer."));
  }

  auto array_reads_per_iteration = GetNamedSettingInt64(
      ac.activity_settings(), "array_reads_per_iteration", 1000);
  if (array_reads_per_iteration < 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("Array reads per iteration (", array_reads_per_iteration,
                     ") must be a positive integer."));
  }
  return absl::OkStatus();
}

void PolluteDataCache::Initialize(ParsedActivityConfig* config) {
  std::srand(time(0));
  auto array_size = config->pollute_data_cache_config.array_size;

  data_array_.resize(array_size);
  for (int i=0; i<array_size; i++) {
    data_array_[i] = i;
  }

  random_index_ = std::uniform_int_distribution<>(0, array_size - 1);
  array_reads_per_iteration_ =
      config->pollute_data_cache_config.array_reads_per_iteration;
  iteration_count_ = 0;

  std::random_device rd;
  rand_gen_ = std::mt19937(rd());
}

void PolluteDataCache::DoActivity() {
  iteration_count_++;
  int64_t sum = 0;
  for (int i = 0; i < array_reads_per_iteration_; i++) {
    // 'rand_gen_' is Mersenne Twister generator.
    int index = random_index_(rand_gen_);

    // This is read and write operation.
    sum += data_array_[index]++;
  }
  optimization_preventing_num_ = sum;
}

ActivityLog PolluteDataCache::GetActivityLog() {
  ActivityLog alog;
  if (iteration_count_) {
    auto* am = alog.add_activity_metrics();
    am->set_name("iteration_count");
    am->set_value_int(iteration_count_);
  }
  return alog;
}

}  // namespace distbench
