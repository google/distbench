// Copyright 2021 Google LLC
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

#include "distbench_node_manager.h"

#include "distbench_utils.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "protocol_driver_allocator.h"
#include "glog/logging.h"

namespace distbench {

TEST(DistBenchNodeManager, Constructor) { NodeManager nm; }

TEST(DistBenchNodeManager, FailNoTestSequencer) {
  NodeManager nm;
  NodeManagerOpts node_manager_opts;
  absl::Status result = nm.Initialize(node_manager_opts);
  ASSERT_FALSE(result.ok());
}

TEST(DistBenchNodeManager, InvalidProtocolName) {
  NodeManager nm;
  grpc::ServerContext context;
  ServiceEndpointMap response;

  NodeServiceConfig request;
  auto traffic_config = request.mutable_traffic_config();
  auto service = traffic_config->add_services();
  service->set_name("root");
  service->set_count(1);
  request.add_services("root/0");

  traffic_config->set_default_protocol("invalid_protocol_name");
  auto ret = nm.ConfigureNode(&context, &request, &response);
  ASSERT_FALSE(ret.ok());
  CHECK_EQ(ret.error_message(),
           "AllocService failure: NOT_FOUND: Could not resolve protocol driver for invalid_protocol_name.");
}

TEST(DistBenchNodeManager, GrpcAlias) {
  NodeManager nm;
  grpc::ServerContext context;
  ServiceEndpointMap response;

  NodeServiceConfig request;
  auto traffic_config = request.mutable_traffic_config();

  auto service = traffic_config->add_services();
  service->set_name("root");
  service->set_count(1);
  request.add_services("root/0");

  traffic_config->set_default_protocol("grpc_alias");
  auto ret = nm.ConfigureNode(&context, &request, &response);
  ASSERT_FALSE(ret.ok());

  auto pdo = traffic_config->add_protocol_driver_options();
  pdo->set_name("grpc_alias");
  pdo->set_protocol_name("grpc");
  ret = nm.ConfigureNode(&context, &request, &response);
  ASSERT_TRUE(ret.ok());
}

TEST(DistBenchNodeManager, AllocateProtocolDriverMaxDepth) {
  NodeManager nm;
  grpc::ServerContext context;
  ServiceEndpointMap response;

  NodeServiceConfig request;
  auto traffic_config = request.mutable_traffic_config();

  auto service = traffic_config->add_services();
  service->set_name("root");
  service->set_count(1);
  request.add_services("root/0");
  traffic_config->set_default_protocol("grpc_alias");
  auto pdo = traffic_config->add_protocol_driver_options();
  pdo->set_name("grpc_alias");
  pdo->set_protocol_name("grpc_alias");
  auto ret = nm.ConfigureNode(&context, &request, &response);
  ASSERT_FALSE(ret.ok());
  CHECK_EQ(ret.error_message(),
           "AllocService failure: FAILED_PRECONDITION: Tree cannot be deeper than max depth of: 4.");
}

}  // namespace distbench
