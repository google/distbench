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

#ifndef DISTBENCH_DISTBENCH_TEST_SEQUENCER_TESTER_H
#define DISTBENCH_DISTBENCH_TEST_SEQUENCER_TESTER_H

#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"

namespace distbench {

class DistBenchTester {
 public:
  ~DistBenchTester();
  absl::Status Initialize();
  absl::StatusOr<TestSequenceResults> RunTestSequence(
      TestSequence test_sequence, int timeout_s);

 private:
  absl::Status Resize(size_t num_nodes);
  std::unique_ptr<TestSequencer> test_sequencer;
  std::unique_ptr<DistBenchTestSequencer::Stub> test_sequencer_stub;
  std::vector<std::unique_ptr<NodeManager>> nodes;
};

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_TEST_SEQUENCER_TESTER_H
