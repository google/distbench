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
#include "joint_distribution_sample_generator.h"

namespace distbench {

absl::StatusOr<std::unique_ptr<DistributionSampleGenerator>>
AllocateSampleGenerator(const DistributionConfig& config) {
  std::unique_ptr<DistributionSampleGenerator> sample_gen_lib;
  sample_gen_lib = std::make_unique<DistributionSampleGenerator>();
  auto status = sample_gen_lib->Initialize(config);
  if (!status.ok()) return status;
  return sample_gen_lib;
};

absl::Status DistributionSampleGenerator::InitializeWithCdf(
    const DistributionConfig& config) {
  DistributionConfig config_with_pmf;
  config_with_pmf.set_name(config.name());

  auto* pmf_point = config_with_pmf.add_pmf_points();
  pmf_point->set_pmf(config.cdf_points(0).cdf());
  auto* data_point = pmf_point->add_data_points();

  if (config.is_cdf_uniform()) {
    data_point->set_lower(0);
    data_point->set_upper(config.cdf_points(0).value());
  } else {
    data_point->set_exact(config.cdf_points(0).value());
  }

  auto prev_cdf = config.cdf_points(0).cdf();
  auto prev_data_value = config.cdf_points(0).value();
  for (int i = 1; i < config.cdf_points_size(); i++) {
    auto curr_cdf = config.cdf_points(i).cdf();
    auto curr_data_value = config.cdf_points(i).value();

    auto* pmf_point = config_with_pmf.add_pmf_points();
    pmf_point->set_pmf(curr_cdf - prev_cdf);

    auto* data_point = pmf_point->add_data_points();
    if (config.is_cdf_uniform()) {
      data_point->set_lower(prev_data_value + 1);
      data_point->set_upper(curr_data_value);
    } else {
      data_point->set_exact(curr_data_value);
    }

    prev_cdf = curr_cdf;
    prev_data_value = curr_data_value;
  }

  return InitializeWithPmf(config_with_pmf);
};

absl::Status DistributionSampleGenerator::InitializeWithPmf(
    const DistributionConfig& config) {
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  generator_ = std::mt19937(seed);
  std::vector<float> pmf;
  num_dimensions_ = config.pmf_points(0).data_points_size();
  exact_value_.resize(num_dimensions_);
  range_.resize(num_dimensions_);
  is_exact_value_.resize(num_dimensions_);
  for (const auto& point : config.pmf_points()) {
    for (int i = 0; i < point.data_points_size(); i++) {
      if (point.data_points(i).has_exact()) {
        exact_value_[i].push_back(point.data_points(i).exact());
        range_[i].push_back(std::uniform_int_distribution<>());
        is_exact_value_[i].push_back(true);
      } else {
        exact_value_[i].push_back(0);
        range_[i].push_back(std::uniform_int_distribution<>(
            point.data_points(i).lower(), point.data_points(i).upper()));
        is_exact_value_[i].push_back(false);
      }
    }
    pmf.push_back(point.pmf());
  }

  distribution_array_ = std::discrete_distribution<int>(pmf.begin(), pmf.end());
  return absl::OkStatus();
};

absl::Status DistributionSampleGenerator::Initialize(
    const DistributionConfig& config) {
  auto status = ValidateDistributionConfig(config);
  if (!status.ok()) return status;
  std::random_device rd;
  mersenne_twister_prng_ = std::mt19937(rd());
  if (config.cdf_points_size()) return InitializeWithCdf(config);
  if (config.pmf_points_size()) return InitializeWithPmf(config);
  return absl::InvalidArgumentError(
      absl::StrCat("Add CDF or PMF to '", config.name(), "'."));
};

std::vector<int> DistributionSampleGenerator::GetRandomSample(
    std::mt19937* generator) {
  auto index = distribution_array_(*generator);
  std::vector<int> sample;

  for (int dim = 0; dim < num_dimensions_; dim++) {
    if (is_exact_value_[dim][index]) {
      sample.push_back(exact_value_[dim][index]);
    } else {
      sample.push_back(range_[dim][index](mersenne_twister_prng_));
    }
  }
  return sample;
};

std::vector<int> DistributionSampleGenerator::GetRandomSample() {
  return GetRandomSample(&generator_);
};

}  // namespace distbench
