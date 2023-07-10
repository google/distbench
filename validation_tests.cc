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

#include "distbench_engine.h"
#include "distbench_utils.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"

namespace distbench {

TestSequence ValidTestSequence() {
  TestSequence sequence;
  auto* test = sequence.add_tests();
  test->set_name("test_config");
  auto* service1 = test->add_services();
  service1->set_name("service1");
  service1->set_count(1);
  auto* service2 = test->add_services();
  service2->set_name("service2");
  service2->set_count(2);

  auto* acl1 = test->add_action_lists();
  acl1->set_name("service1");
  acl1->add_action_names("service1/ping");

  auto* action = test->add_actions();
  action->set_name("service1/ping");
  action->set_rpc_name("echo");
  action->mutable_iterations()->set_max_iteration_count(100);

  auto* rpcd1 = test->add_rpc_descriptions();
  rpcd1->set_name("echo");
  rpcd1->set_client("service1");
  rpcd1->set_server("service2");

  auto* acl2 = test->add_action_lists();
  acl2->set_name("echo");

  auto* dist_config = test->add_distribution_config();
  const float num_points = 8.0;
  dist_config->set_name("simple_distribution");
  dist_config->add_field_names("payload_size");
  for (float i = 1; i <= num_points; ++i) {
    auto* cdf_point = dist_config->add_cdf_points();
    cdf_point->set_cdf(i / num_points);
    cdf_point->set_value(i);
  }
  return sequence;
}

class CheckTestsTest : public ::testing::Test {};

TEST_F(CheckTestsTest, ValidTestSequence) {
  TestSequence sequence = ValidTestSequence();
  auto maybe_canonical = GetCanonicalTestSequence(sequence);
  ASSERT_OK(maybe_canonical.status());
  for (const auto& test : sequence.tests()) {
    auto status = ValidateTrafficConfig(test);
    ASSERT_OK(status);
  }
}

TEST_F(CheckTestsTest, DistConfigWithInvalidDimensions) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* dist_config = test->mutable_distribution_config(0);
  dist_config->add_field_names("request_payload");
  dist_config->add_field_names("response_payload");
  dist_config->add_field_names("payload_size");
  auto status = ValidateTrafficConfig(*test);
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CheckTestsTest, DistConfigsWithSameName) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* dist_config = test->add_distribution_config();
  const float num_points = 10.0;
  dist_config->set_name("simple_distribution");
  dist_config->add_field_names("payload_size");
  for (float i = 1; i <= num_points; ++i) {
    auto* cdf_point = dist_config->add_cdf_points();
    cdf_point->set_cdf(i / num_points);
    cdf_point->set_value(i);
  }
  auto status = ValidateTrafficConfig(*test);
  ASSERT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(CheckTestsTest, NegitiveXSize) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* service = test->add_services();
  service->set_name("rectangular");
  service->set_x_size(-3);
  service->set_y_size(5);
  auto maybe_canonical = GetCanonicalTestSequence(sequence);
  ASSERT_EQ(maybe_canonical.status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(CheckTestsTest, InvalidXZSize) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* service = test->add_services();
  service->set_name("rectangular");
  service->set_x_size(3);
  service->set_z_size(5);
  auto maybe_canonical = GetCanonicalTestSequence(sequence);
  ASSERT_EQ(maybe_canonical.status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(CheckTestsTest, InvalidYZSize) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* service = test->add_services();
  service->set_name("rectangular");
  service->set_y_size(3);
  service->set_z_size(5);
  auto maybe_canonical = GetCanonicalTestSequence(sequence);
  ASSERT_EQ(maybe_canonical.status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(CheckTestsTest, InvalidYSizeEqualZero) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* service = test->add_services();
  service->set_name("rectangular");
  service->set_x_size(3);
  service->set_y_size(0);
  auto maybe_canonical = GetCanonicalTestSequence(sequence);
  ASSERT_EQ(maybe_canonical.status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(CheckTestsTest, XYZsizeNotEqualCount) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* service = test->add_services();
  service->set_name("rectangular");
  service->set_x_size(3);
  service->set_y_size(5);
  service->set_count(1);
  auto maybe_canonical = GetCanonicalTestSequence(sequence);
  ASSERT_EQ(maybe_canonical.status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(CheckTestsTest, InvalidRPC) {
  TestSequence sequence = ValidTestSequence();
  auto* test = sequence.mutable_tests(0);
  auto* rpc = test->add_rpc_descriptions();
  rpc->set_name("msg");
  rpc->set_client("service1");

  auto status = ValidateTrafficConfig(*test);
  ASSERT_EQ(status.code(), absl::StatusCode::kNotFound);
}

}  // namespace distbench