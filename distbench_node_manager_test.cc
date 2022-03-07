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

namespace distbench {

TEST(DistBenchNodeManager, Constructor) { NodeManager nm; }

TEST(DistBenchNodeManager, FailNoTestSequencer) {
  NodeManager nm;
  NodeManagerOpts node_manager_opts;
  absl::Status result = nm.Initialize(node_manager_opts);
  ASSERT_FALSE(result.ok());
}

}  // namespace distbench
