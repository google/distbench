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

#include "boost/preprocessor/repetition/repeat.hpp"
#include "glog/logging.h"

namespace distbench {

std::unique_ptr<Activity> AllocateActivity(ParsedActivityConfig* config) {
  std::unique_ptr<Activity> activity;
  auto activity_func = config->activity_func;

  if (activity_func == "ConsumeCpu") {
    activity = std::make_unique<ConsumeCpu>();
  } else if (activity_func == "PolluteDataCache") {
    activity = std::make_unique<PolluteDataCache>();
  } else if (activity_func == "PolluteInstructionCache") {
    activity = std::make_unique<PolluteInstructionCache>();
  }

  activity->Initialize(config);
  return activity;
}

// Activity: ConsumeCpu

void ConsumeCpu::DoActivity() {
  iteration_count_++;
  unsigned int sum = 0;
  std::srand(time(0));
  std::generate(rand_array.begin(), rand_array.end(), std::rand);
  std::sort(rand_array.begin(), rand_array.end());
  for (auto num : rand_array) sum += num;
  optimization_preventing_num_ = sum;
}

ActivityLog ConsumeCpu::GetActivityLog() {
  ActivityLog alog;
  if (iteration_count_) {
    auto* am = alog.add_activity_metrics();
    am->set_name("iteration_count");
    am->set_value_int(iteration_count_);
  }
  return alog;
}

void ConsumeCpu::Initialize(ParsedActivityConfig* config) {
  rand_array.resize(config->consume_cpu_config.array_size);
  iteration_count_ = 0;
}

absl::Status ConsumeCpu::ValidateConfig(ActivityConfig& ac) {
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
  for (int i = 0; i < array_size; i++) {
    data_array_[i] = i;
  }

  random_index_ = std::uniform_int_distribution<>(0, array_size - 1);
  array_reads_per_iteration_ =
      config->pollute_data_cache_config.array_reads_per_iteration;
  iteration_count_ = 0;

  std::random_device rd;
  mersenne_twister_prng_ = std::mt19937(rd());
}

void PolluteDataCache::DoActivity() {
  iteration_count_++;
  int64_t sum = 0;
  for (int i = 0; i < array_reads_per_iteration_; i++) {
    int index = random_index_(mersenne_twister_prng_);

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

// Activity: PolluteInstructionCache

// The boost library (https://github.com/nelhage/rules_boost) is used to
// create a 2D array of functions with names -
//
// DummyFunc_0_0, DummyFunc_0_1, ..., DummyFunc_0_{POLLUTE_ICACHE_LOOP_SIZE-1},
// ...
// ...
// ...
// DummyFunc_{POLLUTE_ICACHE_LOOP_SIZE-1}_0, ...,
// DummyFunc_{POLLUTE_ICACHE_LOOP_SIZE-1}_{POLLUTE_ICACHE_LOOP_SIZE-1}. Each of
// these functions returns an int.
//
// We create this 2D array because BOOST_PP_REPEAT() macro can
// generate at most 255 repetitions.
#define POLLUTE_ICACHE_LOOP_SIZE 100
#define GENERATE_FUNC_INNER_LOOP(z, n, prefix)                           \
  int DummyFunc_##prefix##_##n(bool do_work) {                           \
    /*                                                                   \
     * The purpose of 'do_work' part of the function is only to increase \
     * the size of the function.                                         \
     */                                                                  \
    if (do_work) {                                                       \
      int result = 0;                                                    \
      for (int i = 0; i < n; i++) {                                      \
        result += i * i;                                                 \
        result /= i + 1;                                                 \
        result -= i / 2;                                                 \
        result /= i * i;                                                 \
        result += result * i;                                            \
        result /= result + 1;                                            \
        result -= result / 2;                                            \
        result *= result;                                                \
      }                                                                  \
      return result;                                                     \
    } else                                                               \
      return n;                                                          \
  }
#define GENERATE_FUNC_OUTER_LOOP(z, n, text) \
  BOOST_PP_REPEAT(POLLUTE_ICACHE_LOOP_SIZE, GENERATE_FUNC_INNER_LOOP, n)

// Get the pointers to the functions created above and store it in
// func_ptr_array.
#define GET_FUNC_PTR_INNER_LOOP(z, n, prefix)                \
  func_ptr_array_[(prefix * POLLUTE_ICACHE_LOOP_SIZE + n)] = \
      &DummyFunc_##prefix##_##n;
#define GET_FUNC_PTR_OUTER_LOOP(z, n, text) \
  BOOST_PP_REPEAT(POLLUTE_ICACHE_LOOP_SIZE, GET_FUNC_PTR_INNER_LOOP, n)

// Generate the functions for instruction cache miss.
BOOST_PP_REPEAT(POLLUTE_ICACHE_LOOP_SIZE, GENERATE_FUNC_OUTER_LOOP, );

absl::Status PolluteInstructionCache::ValidateConfig(ActivityConfig& ac) {
  return absl::OkStatus();
}

void PolluteInstructionCache::Initialize(ParsedActivityConfig* config) {
  auto func_array_size = POLLUTE_ICACHE_LOOP_SIZE * POLLUTE_ICACHE_LOOP_SIZE;
  func_ptr_array_.resize(func_array_size);
  BOOST_PP_REPEAT(POLLUTE_ICACHE_LOOP_SIZE, GET_FUNC_PTR_OUTER_LOOP, );

  std::random_device rd;
  mersenne_twister_prng_ = std::mt19937(rd());

  function_invocations_per_iteration_ = config->pollute_instruction_cache_config
                                            .function_invocations_per_iteration;

  random_index_ = std::uniform_int_distribution<>(0, func_array_size - 1);

  iteration_count_ = 0;
}

void PolluteInstructionCache::DoActivity() {
  iteration_count_++;
  int64_t sum = 0;

  for (int i = 0; i < function_invocations_per_iteration_; i++) {
    int index = random_index_(mersenne_twister_prng_);
    sum += func_ptr_array_[index](false);
  }
  optimization_preventer_ = sum;
}

ActivityLog PolluteInstructionCache::GetActivityLog() {
  ActivityLog alog;
  if (iteration_count_) {
    auto* am = alog.add_activity_metrics();
    am->set_name("iteration_count");
    am->set_value_int(iteration_count_);
  }
  return alog;
}

}  // namespace distbench
