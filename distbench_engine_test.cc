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

#include "distbench_engine.h"

#include "distbench_utils.h"
#include "protocol_driver_allocator.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"

namespace distbench {

ProtocolDriverOptions grpc_pd_opts() {
  auto opts = ProtocolDriverOptions();
  opts.set_protocol_name("grpc");
  return opts;
}

TEST(DistBenchEngineTest, ctor) {
  DistBenchEngine dbe(AllocateProtocolDriver(grpc_pd_opts()).value());
}

TEST(DistBenchEngineTest, init) {
  DistributedSystemDescription desc;
  desc.add_services()->set_name("test_service");
  DistBenchEngine dbe(AllocateProtocolDriver(grpc_pd_opts()).value());
  int port = 0;
  ASSERT_OK(dbe.Initialize(desc, "test_service", 0, &port));
}

}  // namespace distbench
