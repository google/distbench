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
#ifndef DISTBENCH_JOINT_DISTRIBUTION_SAMPLE_GENERATOR_H_
#define DISTBENCH_JOINT_DISTRIBUTION_SAMPLE_GENERATOR_H_

#include <array>
#include <chrono>
#include <functional>
#include <iostream>

#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "joint_distribution.pb.h"

namespace distbench {

class DistributionSampleGenerator {
 public:
  ~DistributionSampleGenerator(){};

  absl::Status Initialize(const DistributionConfig& config);
  std::vector<int> GetRandomSample();
  std::vector<int> GetRandomSample(absl::BitGen* generator);

 private:
  int num_dimensions_;

  absl::BitGen generator_;
  std::discrete_distribution<int> distribution_array_;

  std::vector<std::vector<int>> exact_value_;

  std::vector<std::vector<std::uniform_int_distribution<>>> range_;

  std::vector<std::vector<bool>> is_exact_value_;

  absl::Status InitializeWithPmf(const DistributionConfig& config);
  absl::Status InitializeWithCdf(const DistributionConfig& config);
};

absl::StatusOr<std::unique_ptr<DistributionSampleGenerator>>
AllocateSampleGenerator(const DistributionConfig& config);

}  // namespace distbench

#endif  // DISTBENCH_JOINT_DISTRIBUTION_SAMPLE_GENERATOR_H_
