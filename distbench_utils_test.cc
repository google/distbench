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

#include <sys/socket.h>

#include "distbench_netutils.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"

namespace distbench {

const char* canonical_1d_fields[] = {"payload_size", nullptr};

const char* canonical_2d_fields[] = {"request_payload_size",
                                     "response_payload_size", nullptr};

// This test makes a distribution config with 8 CDF points and a name
// then creates a canonical version of it and tests that the number of
// PMF points and name are equivalent to the original config.
TEST(DistBenchUtilsTest, GetCanonicalCdf) {
  const int num_cdf_points = 8;
  DistributionConfig config;
  config.set_name("test_config");
  config.add_field_names(canonical_1d_fields[0]);
  for (float i = 1; i <= num_cdf_points; ++i) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_cdf(i / num_cdf_points);
    cdf_point->set_value(i);
  }
  ASSERT_EQ(config.name(), "test_config");
  ASSERT_EQ(config.cdf_points_size(), num_cdf_points);
  auto temp = GetCanonicalDistributionConfig(config, canonical_1d_fields);
  ASSERT_OK(temp.status());
  auto canonical = temp.value();
  ASSERT_EQ(canonical.name(), "test_config");
  ASSERT_EQ(canonical.cdf_points_size(), 0);
  ASSERT_EQ(canonical.pmf_points_size(), num_cdf_points);
  ASSERT_EQ(canonical.field_names_size(), 1);
}

// This test makes a distribution config with a name, 4 PMF points,
// and 2 field names then creates a canonical version of the config and
// tests that the name is equivalent, and the number of points are equal.
TEST(DistBenchUtilsTest, GetCanonicalPmf) {
  const int num_pmf_points = 4;
  const int sum_of_pmf_points = 10;
  DistributionConfig config;
  config.set_name("test_config");
  config.add_field_names(canonical_1d_fields[0]);
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / sum_of_pmf_points);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }

  ASSERT_EQ(config.name(), "test_config");
  auto temp = GetCanonicalDistributionConfig(config, canonical_1d_fields);
  ASSERT_OK(temp.status());
  auto canonicalTwo = temp.value();
  ASSERT_EQ(canonicalTwo.name(), "test_config");
  ASSERT_EQ(canonicalTwo.cdf_points_size(), 0);
  ASSERT_EQ(canonicalTwo.pmf_points_size(), num_pmf_points);
}

// This results in a total of all pmfs that is not-quite 1.0
// but close enough that it should be accepted by the library:
TEST(DistBenchUtilsTest, GetCanonicalPmfNearOne) {
  const int num_pmf_points = 5;
  const int sum_of_pmf_points = 15;
  DistributionConfig config;
  config.set_name("test_config");
  config.add_field_names(canonical_1d_fields[0]);
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / sum_of_pmf_points);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }

  ASSERT_EQ(config.name(), "test_config");
  auto temp = GetCanonicalDistributionConfig(config, canonical_1d_fields);
  ASSERT_OK(temp.status());
  auto canonicalTwo = temp.value();
  ASSERT_EQ(canonicalTwo.name(), "test_config");
  ASSERT_EQ(canonicalTwo.cdf_points_size(), 0);
  ASSERT_EQ(canonicalTwo.pmf_points_size(), num_pmf_points);
}

// This test creates a distribution config with PMF points with only one
// datapoint and two field dimensions and makes sure the invalid argument
// error is returned.
TEST(DistBenchUtilsTest, GetCanonicalInvalidDimensions) {
  DistributionConfig config;
  const int num_pmf_points = 4;
  const int sum_of_pmf_points = 10;
  config.set_name("invalid_dimensions_config");
  config.add_field_names(canonical_2d_fields[0]);
  config.add_field_names(canonical_2d_fields[1]);
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / sum_of_pmf_points);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  auto status =
      GetCanonicalDistributionConfig(config, canonical_1d_fields).status();
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

// This test creates an invalid distribution config with a decreasing value
// tests that an error status is returned.
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
  auto status =
      GetCanonicalDistributionConfig(config, canonical_1d_fields).status();
  ASSERT_EQ(status,
            absl::InvalidArgumentError(
                "The value:'6' must be greater than previous_value:'7' at "
                "index '1' in CDF:'invalid_CDF_config'."));
}

// This test creates an distribution config whose pmf values sum to 1/2, and
// tests that an error status is returned.
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
  config.add_field_names(canonical_1d_fields[0]);
  auto status =
      GetCanonicalDistributionConfig(config, canonical_1d_fields).status();
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

// This test creates an invalid config with both CDF and PMF points and
// tets that an error status is returned.
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
  auto status =
      GetCanonicalDistributionConfig(config, canonical_1d_fields).status();
  ASSERT_EQ(
      status,
      absl::InvalidArgumentError(
          "Exactly one of CDF and PMF must be provided for 'invalid_config'."));
}

TEST(DistBenchUtilsTest, uniformCDFdistribution) {
  const int num_pmf_points = 7;
  DistributionConfig config;
  const int kBucketWidth = 101;
  const int kStartValue = 1976;
  std::vector<int> lower_bounds;
  std::vector<int> upper_bounds;
  lower_bounds.reserve(num_pmf_points + 1);
  upper_bounds.reserve(num_pmf_points + 1);
  config.set_name("uniform_dist");
  config.add_field_names(canonical_1d_fields[0]);
  lower_bounds.push_back(kStartValue);
  auto* cdf_point = config.add_cdf_points();
  cdf_point->set_cdf(0);
  cdf_point->set_value(kStartValue);
  for (float i = 1; i <= num_pmf_points; ++i) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_cdf(i / num_pmf_points);
    cdf_point->set_value(kStartValue + i * kBucketWidth);
    upper_bounds.push_back(cdf_point->value());
    lower_bounds.push_back(cdf_point->value() + 1);
  }
  upper_bounds.push_back(kStartValue + (num_pmf_points + 1) * kBucketWidth);
  auto temp = GetCanonicalDistributionConfig(config, canonical_1d_fields);
  ASSERT_OK(temp.status());
  auto canonical = temp.value();
  ASSERT_EQ(canonical.pmf_points_size(), num_pmf_points);
  for (int i = 0; i < num_pmf_points - 1; ++i) {
    auto datapoint = canonical.pmf_points(i).data_points(0);
    EXPECT_EQ(datapoint.lower(), lower_bounds[i]);
    EXPECT_EQ(datapoint.upper(), upper_bounds[i]);
  }
}

TEST(DistributionSampleGeneratorTest, NoDistributionConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(
      status,
      absl::InvalidArgumentError(
          "Exactly one of CDF and PMF must be provided for 'MyReqPayloadDC'."));
}

TEST(DistributionSampleGeneratorTest, BothCdfAndPdfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");

  auto* pmf_point = config.add_pmf_points();
  pmf_point->set_pmf(1);
  auto* data_point = pmf_point->add_data_points();
  data_point->set_exact(10);

  auto* cdf_point = config.add_cdf_points();
  cdf_point->set_cdf(1);
  cdf_point->set_value(10);

  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(
      status,
      absl::InvalidArgumentError(
          "Exactly one of CDF and PMF must be provided for 'MyReqPayloadDC'."));
}

TEST(DistributionSampleGeneratorTest, ValidateDistributionPmfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  for (int i = 1; i < 5; i++) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / 10.0);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_OK(status);
}

TEST(DistributionSampleGeneratorTest, InvalidDistributionPmfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  for (int i = 1; i < 5; i++) {
    auto* pmf_point = config.add_pmf_points();
    pmf_point->set_pmf(i / 20.0);
    auto* data_point = pmf_point->add_data_points();
    data_point->set_exact(i);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(DistributionSampleGeneratorTest, ValidateDistributionCdfConfig) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i);
    cdf += i / 10.0;
    cdf_point->set_cdf(cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_OK(status);
}

TEST(DistributionSampleGeneratorTest,
     InvalidDistributionCdfConfigErraneousCdf) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i);
    cdf += i / 20.0;
    cdf_point->set_cdf(cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status,
            absl::InvalidArgumentError(
                "The maximum value of cdf is '0.5' in CDF:'MyReqPayloadDC'. "
                "It must be exactly equal to 1."));
}

TEST(DistributionSampleGeneratorTest,
     InvalidDistributionCdfConfigNonIncreasingValues) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(100 - 10 * i);
    cdf += i / 20.0;
    cdf_point->set_cdf(cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status,
            absl::InvalidArgumentError(
                "The value:'80' must be greater than previous_value:'90' at "
                "index '1' in CDF:'MyReqPayloadDC'."));
}

TEST(DistributionSampleGeneratorTest,
     InvalidDistributionCdfConfigNonIncreasingCdf) {
  DistributionConfig config;
  config.set_name("MyReqPayloadDC");
  float cdf = 0;
  for (int i = 1; i < 5; i++) {
    auto* cdf_point = config.add_cdf_points();
    cdf_point->set_value(i);
    cdf += i / 20.0;
    cdf_point->set_cdf(1 / cdf);
  }
  auto status = ValidateDistributionConfig(config);
  ASSERT_EQ(status,
            absl::InvalidArgumentError(
                "The cdf value:'6.66667' must be greater than previous cdf "
                "value:'20' at index '1' in CDF:'MyReqPayloadDC'."));
}

TEST(IP6to4IsPrivate, Private6to4) {
  DeviceIpAddress addr("2002:ad3:e902::1", "eth0", AF_INET6);
  EXPECT_TRUE(addr.isPrivate());
}

TEST(IP6to4IsPrivate, NotPrivate6to4) {
  DeviceIpAddress addr("2002:1ad3:e902::1", "eth0", AF_INET6);
  EXPECT_FALSE(addr.isPrivate());
}

TEST(IP6to4IsPrivate, Private6) {
  DeviceIpAddress addr("fc00:1ad3:e902::1", "eth0", AF_INET6);
  EXPECT_TRUE(addr.isPrivate());
}

TEST(IP6to4IsPrivate, NotPrivate6) {
  DeviceIpAddress addr("1234:1ad3:e902::1", "eth0", AF_INET6);
  EXPECT_FALSE(addr.isPrivate());
}

TEST(SetSerializedSize, LargeRangesExact) {
  GenericRequest request;
  // Interesting things happen near powers of 128, so we test near
  // 128, 128^2, 128^3, 128^4
  for (int i = 1; i < 5; ++i) {
    int size = 1 << (7 * i);
    // We test a range of values in the vicinity of each power of 128:
    for (int j = -16; j < 16; ++j) {
      SetSerializedSize(&request, size + j);
      ASSERT_EQ(request.ByteSizeLong(), size + j);
    }
  }
}

TEST(SetSerializedSize, SmallRangeCloseEnough) {
  GenericRequest request;
  for (int i = 0; i < 256; ++i) {
    SetSerializedSize(&request, i);
    ASSERT_GE(request.ByteSizeLong(), i);
  }
}

}  // namespace distbench
