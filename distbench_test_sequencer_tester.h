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

#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"

namespace distbench {

std::unique_ptr<grpc::ClientContext> CreateContextWithDeadline(int max_time_s);

class DistBenchTester {
 public:
  ~DistBenchTester();
  absl::Status Initialize(size_t num_nodes = 0);
  absl::StatusOr<TestSequenceResults> RunTestSequence(
      TestSequence test_sequence, int timeout_s);

 private:
  absl::Status Resize(size_t num_nodes);
  std::unique_ptr<TestSequencer> test_sequencer;
  std::unique_ptr<DistBenchTestSequencer::Stub> test_sequencer_stub;
  std::vector<std::unique_ptr<NodeManager>> nodes;
};

}  // namespace distbench
