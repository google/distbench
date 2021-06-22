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

class DistBenchEngineTest : public testing::Test {
 protected:
  // Per-test-suite set-up.
  static void SetUpTestSuite() {
    port_allocator_ = new PortAllocator();
    port_allocator_->AddPortsToPoolFromString("20100-20199");
  }

  // Per-test-suite tear-down.
  static void TearDownTestSuite() {
    delete port_allocator_;
    port_allocator_ = nullptr;
  }

  // You can define per-test set-up logic as usual.
  void SetUp() override { }

  // You can define per-test tear-down logic as usual.
  void TearDown() override { }

  // Shared PortAllocator
  static PortAllocator* port_allocator_;
};

PortAllocator* DistBenchEngineTest::port_allocator_= nullptr;

TEST_F(DistBenchEngineTest, ctor) {
  RealClock clock;
  DistBenchEngine dbe(AllocateProtocolDriver(grpc_pd_opts()), &clock);
}

TEST_F(DistBenchEngineTest, init) {
  DistributedSystemDescription desc;
  desc.add_services()->set_name("test_service");
  RealClock clock;
  DistBenchEngine dbe(AllocateProtocolDriver(grpc_pd_opts()), &clock);
  int port = port_allocator_->AllocatePort();
  ASSERT_OK(dbe.Initialize(port, desc, "test_service", 0));
  port_allocator_->ReleasePort(port);
}

}  // namespace distbench
