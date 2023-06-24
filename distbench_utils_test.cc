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

#include "glog/logging.h"
#include "gtest/gtest.h"

namespace distbench {

/**
 * This test makes a distribution config with 8 CDF points and a name
 * then creates a canonical version of it and tests that the number of
 * PMF points and name are equivalent to the original config.
 */
TEST(DistBenchUtilsTest, GetCanonicalCdf) {
  const int num_cdf_points = 8;
  DistributionConfig config;
  config.set_name("test_config");
  for (float i = 1; i <= num_cdf_points; ++i) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_cdf(i / num_cdf_points);
    cdf_point->set_value(i);
  }
  ASSERT_EQ(config.name(), "test_config");
  ASSERT_EQ(config.cdf_points_size(), num_cdf_points);
  auto temp = GetCanonicalDistributionConfig(config);
  auto canonical = temp.value();
  ASSERT_EQ(canonical.name(), "test_config");
  ASSERT_EQ(canonical.cdf_points_size(), 0);
  ASSERT_EQ(canonical.pmf_points_size(), num_cdf_points);
  ASSERT_EQ(canonical.field_names_size(), 0);
}

/**
 * This test makes a distribution config with a name, 4 PMF points,
 * and 2 field names then creates a canonical version of the config and
 * tests that the name is equivalent, and the number of points are equal.
 */
TEST(DistBenchUtilsTest, GetCanonicalPmf) {
  const int num_pmf_points = 4;
  const int sum_of_pmf_points = 10;
  DistributionConfig configTwo;
  configTwo.set_name("another_config");
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* pmf_point = configTwo.add_pmf_points();
    pmf_point->set_pmf(i / sum_of_pmf_points);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  configTwo.add_field_names("request_payload_size");

  ASSERT_EQ(configTwo.name(), "another_config");
  auto temp = GetCanonicalDistributionConfig(configTwo);
  auto canonicalTwo = temp.value();
  ASSERT_EQ(canonicalTwo.name(), "another_config");
  ASSERT_EQ(canonicalTwo.cdf_points_size(), 0);
  ASSERT_EQ(canonicalTwo.pmf_points_size(), num_pmf_points);
}

// This results in a total of all pmfs that is not-quite 1.0
// but close enough that it should be accepted by the library:
TEST(DistBenchUtilsTest, GetCanonicalPmfNearOne) {
  const int num_pmf_points = 5;
  const int sum_of_pmf_points = 15;
  DistributionConfig configTwo;
  configTwo.set_name("another_config");
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* pmf_point = configTwo.add_pmf_points();
    pmf_point->set_pmf(i / sum_of_pmf_points);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  configTwo.add_field_names("request_payload_size");

  ASSERT_EQ(configTwo.name(), "another_config");
  auto temp = GetCanonicalDistributionConfig(configTwo);
  auto canonicalTwo = temp.value();
  ASSERT_EQ(canonicalTwo.name(), "another_config");
  ASSERT_EQ(canonicalTwo.cdf_points_size(), 0);
  ASSERT_EQ(canonicalTwo.pmf_points_size(), num_pmf_points);
}

/**
 * This test creates a distribution config with PMF points with only one
 * datapoint and two field dimensions and makes sure the invalid argument
 * error is returned.
 */
TEST(DistBenchUtilsTest, GetCanonicalInvalidDimensions) {
  DistributionConfig config;
  const int num_pmf_points = 4;
  const int sum_of_pmf_points = 10;
  config.set_name("invalid_dimensions_config");
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / sum_of_pmf_points);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  config.add_field_names("request_payload_size");
  config.add_field_names("response_payload_size");
  auto status = GetCanonicalDistributionConfig(config).status();
  ASSERT_EQ(status,
            absl::InvalidArgumentError("The number of field dimensions"
                                       " and PMF datapoints do not match."));
}

/**
 * This test creates an invalid distribution config with a decreasing value
 * tests that an error status is returned.
 */
TEST(DistBenchUtilsTest, GetCanonicalInvalidCDFconfig) {
  const int num_cdf_points = 8;
  DistributionConfig config;
  config.set_name("invalid_CDF_config");
  for (float i = 1; i <= num_cdf_points; ++i) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_cdf(i / num_cdf_points);
    // invalid, values must increase
    cdf_point->set_value(num_cdf_points - i);
  }
  ASSERT_EQ(config.cdf_points_size(), num_cdf_points);
  auto canonical = GetCanonicalDistributionConfig(config).status();
  ASSERT_EQ(canonical,
            absl::InvalidArgumentError(
                "The value:'6' must be greater than previous_value:'7' at "
                "index '1' in CDF:'invalid_CDF_config'."));
}

/**
 * This test creates an invalid distribution config with 4 PMF points and
 * tests that an error status is returned.
 */
TEST(DistBenchUtilsTest, GetCanonicalInvalidPMFconfig) {
  DistributionConfig config;
  const int num_pmf_points = 4;
  const int sum_of_pmf_points = 10;
  config.set_name("invalid_PMF_config");
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / (sum_of_pmf_points * 2));
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
    data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  config.add_field_names("request_payload_size");
  config.add_field_names("response_payload_size");
  auto status = GetCanonicalDistributionConfig(config).status();
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

/**
 * This test creates an invalid config with both CDF and PMF points and
 * tets that an error status is returned.
 */
TEST(DistBenchUtilsTest, GetCanonicalInvalidDistributionConfig) {
  DistributionConfig config;
  const int num_points = 4;
  config.set_name("invalid_config");
  for (float i = 1; i < num_points; i += 2) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / num_points);
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_cdf((i + 1) / num_points);
    cdf_point->set_value(i * 10000000);
  }
  auto canonical = GetCanonicalDistributionConfig(config).status();
  ASSERT_EQ(
      canonical,
      absl::InvalidArgumentError(
          "Exactly one of CDF and PMF must be provided for 'invalid_config'."));
}

TEST(DistBenchUtilsTest, uniformCDFdistribution) {
  const int num_cdf_points = 8;
  DistributionConfig config;
  int counter = 101;
  std::vector<int> lower(num_cdf_points - 1, -1);
  std::vector<int> upper(num_cdf_points - 1, -1);
  config.set_name("unifrom_dist");
  for (int i = 0; i < num_cdf_points - 1; ++i) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_cdf(i / num_cdf_points);
    cdf_point->set_value(counter);
    lower[i] = counter;
    counter += 101;
    upper[i] = counter;
  }
  auto* cdf_point = config.add_cdf_points();
  cdf_point->set_cdf(num_cdf_points / num_cdf_points);
  cdf_point->set_value(counter);
  auto temp = GetCanonicalDistributionConfig(config);
  auto canonical = temp.value();
  ASSERT_EQ(canonical.pmf_points_size(), num_cdf_points - 1);
  for (int i = 0; i < num_cdf_points - 1; ++i) {
    auto datapoint = canonical.pmf_points(i).data_points(1);
    ASSERT_EQ(datapoint.lower(), lower[i]);
    ASSERT_EQ(datapoint.upper(), upper[i] - 1);
  }
}

}  // namespace distbench
