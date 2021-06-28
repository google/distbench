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

#include "base/logging.h"
#include "distbench_utils.h"
#include "protocol_driver_allocator.h"
#include "testing/base/public/benchmark.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"

namespace distbench {

ProtocolDriverOptions common_pd_opts() {
  auto opts = ProtocolDriverOptions();
  opts.set_protocol_name("grpc");
  return opts;
}

TEST(DistBenchEngineTest, ctor) {
  RealClock clock;
  DistBenchEngine dbe(AllocateProtocolDriver(common_pd_opts()), &clock);
}

TEST(DistBenchEngineTest, init) {
  DistributedSystemDescription desc;
  desc.add_services()->set_name("test_service");
  RealClock clock;
  DistBenchEngine dbe(AllocateProtocolDriver(common_pd_opts()), &clock);
  int port = 0;
  ASSERT_OK(dbe.Initialize(desc, "test_service", 0, &port));
}

}  // namespace distbench
