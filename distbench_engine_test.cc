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
#include "protocol_driver_allocator.h"

namespace distbench {

ProtocolDriverOptions grpc_pd_opts() {
  auto opts = ProtocolDriverOptions();
  opts.set_protocol_name("grpc");
  return opts;
}

TEST(DistBenchEngineTest, Constructor) {
  int port = 0;
  DistBenchEngine dbe(AllocateProtocolDriver(grpc_pd_opts(), &port).value());
}

TEST(DistBenchEngineTest, Initialization) {
  DistributedSystemDescription desc;
  desc.add_services()->set_name("test_service");
  int driver_port = 0;
  DistBenchEngine dbe(
      AllocateProtocolDriver(grpc_pd_opts(), &driver_port).value());
  int engine_port = 0;
  ASSERT_OK(dbe.Initialize(desc, "", "test_service", 0, &engine_port));
}

}  // namespace distbench
