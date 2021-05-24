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
#include "distbench_test_sequencer.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include <glog/logging.h>

ABSL_FLAG(int, port, 10000, "port to listen on");
ABSL_FLAG(std::string, test_sequencer, "", "host:port of test sequencer");

void Usage() {
}

int main(int argc, char** argv, char** envp) {
  if (argc < 2) {
    Usage();
    return(1);
  }
  absl::ParseCommandLine(argc, argv);
  distbench::InitLibs();
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "test_sequencer")) {
      distbench::TestSequencerOpts opts = {};
      opts.port = absl::GetFlag(FLAGS_port);
      distbench::TestSequencer test_sequencer;
      test_sequencer.Initialize(opts);
      test_sequencer.Wait();
      return 0;
    }
    if (!strcmp(argv[i], "node_manager")) {
      distbench::NodeManagerOpts opts = {};
      opts.test_sequencer_service_address = absl::GetFlag(FLAGS_test_sequencer);
      opts.port = absl::GetFlag(FLAGS_port);
      distbench::RealClock clock;
      distbench::NodeManager node_manager(&clock);
      absl::Status status = node_manager.Initialize(opts);
      if (!status.ok()) {
        LOG(ERROR) << status;
      }
      node_manager.Wait();
      return !status.ok();
    }
  }
  Usage();
  return(1);
}
