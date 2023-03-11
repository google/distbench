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
#ifndef DISTBENCH_ACTIVITY_H_
#define DISTBENCH_ACTIVITY_H_

#include <random>

#include "absl/status/statusor.h"
#include "distbench.pb.h"
#include "simple_clock.h"

namespace distbench {

struct ParsedActivityConfig {
  std::string activity_config_name;
  std::string activity_func;
  int array_size;
  int array_reads_per_iteration;
  int function_invocations_per_iteration;
  absl::Duration sleepfor_duration;
};

absl::StatusOr<ParsedActivityConfig> ParseActivityConfig(ActivityConfig& ac);

// Base class for activities that run along with RPCs in distbench.
// Activities can be used to simulate various activities occuring in real world,
// for eg. RPC processing delays, CPU work, cache corruption, etc.
class Activity {
 public:
  virtual ~Activity(){};

  // Initializes the class members from configuration provided
  // in ActivityConfig.
  virtual void Initialize(ParsedActivityConfig* config, SimpleClock* clock) = 0;

  // Executes the Activity present in the class. This function is called from
  // DistBenchEngine::ActionState's iteration_function by
  // DistBenchEngine::RunAction method. As a result this method is called
  // multiple times in a loop until the Activity is cancelled.
  virtual void DoActivity() = 0;

  // Returns an ActivityLog containing results metrics of Activity's run.
  virtual ActivityLog GetActivityLog() = 0;
};

// Returns a unique_ptr to a newly instantiated Activity as described by the
// configuration in ActivityConfig.
std::unique_ptr<Activity> AllocateActivity(ParsedActivityConfig* config,
                                           SimpleClock* clock);

class ConsumeCpu : public Activity {
 public:
  void DoActivity() override;
  ActivityLog GetActivityLog() override;
  void Initialize(ParsedActivityConfig* config, SimpleClock* clock) override;
  static absl::Status ValidateConfig(ActivityConfig& ac);

 private:
  std::vector<int> rand_array;
  int iteration_count_ = 0;
  int64_t optimization_preventing_num_ = 0;
};

class PolluteDataCache : public Activity {
 public:
  static absl::Status ValidateConfig(ActivityConfig& ac);
  void Initialize(ParsedActivityConfig* config, SimpleClock* clock) override;
  void DoActivity() override;
  ActivityLog GetActivityLog() override;

 private:
  int iteration_count_ = 0;
  int array_reads_per_iteration_ = 0;
  std::vector<int> data_array_;
  int64_t optimization_preventing_num_ = 0;
  std::uniform_int_distribution<> random_index_;
  std::mt19937 mersenne_twister_prng_;
};

class PolluteInstructionCache : public Activity {
 public:
  static absl::Status ValidateConfig(ActivityConfig& ac);
  void Initialize(ParsedActivityConfig* config, SimpleClock* clock) override;
  void DoActivity() override;
  ActivityLog GetActivityLog() override;

 private:
  int iteration_count_ = 0;
  int function_invocations_per_iteration_ = 0;
  std::mt19937 mersenne_twister_prng_;
  std::vector<int (*)(bool)> func_ptr_array_;
  std::uniform_int_distribution<> random_index_;
};

class SleepFor : public Activity {
 public:
  static absl::Status ValidateConfig(ActivityConfig& ac);
  void Initialize(ParsedActivityConfig* config, SimpleClock* clock) override;
  void DoActivity() override;
  ActivityLog GetActivityLog() override;

 private:
  SimpleClock* clock_ = nullptr;
  absl::Duration duration_;
};

}  // namespace distbench

#endif  // ACTIVITY_H_
