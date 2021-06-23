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

ABSL_FLAG(int, port, 10000, "port to listen on");
ABSL_FLAG(std::string, test_sequencer, "", "host:port of test sequencer");
ABSL_FLAG(bool, use_ipv4_first, false,
    "Prefer IPv4 addresses to IPv6 addresses when both are available");
ABSL_FLAG(std::string, ports_to_use, "10001-11000", "ports to available to use");

void Usage() {
  std::cerr << "Usage: distbench module [options]\n";
  std::cerr << "\n";
  std::cerr << "  module: the module to start (test_sequencer|node_manager)\n";
  std::cerr << "\n";
  std::cerr << "  distbench test_sequencer [--port=port_number]\n";
  std::cerr << "      --port=port_number    The port for the "
                                 "test_sequencer to listen on.\n";
  std::cerr << "  distbench node_manager [--test_sequencer=host:port] "
                                        "[--port=port_number]\n";
  std::cerr << "      --test_sequencer=h:p  The host:port of the "
                          "test_sequencer to connect to.\n";
  std::cerr << "      --port=port_number    The port for the "
                                 "node_manager to listen on.\n";

  std::cerr << "\n";
  std::cerr << "For more options information, do\n";
  std::cerr << "  distbench --helpfull\n";
}

int main(int argc, char** argv, char** envp) {
  if (argc < 2) {
    Usage();
    return 1;
  }

  absl::ParseCommandLine(argc, argv);
  distbench::InitLibs(argv[0]);

  distbench::PortAllocator &port_allocator = distbench::GetMainPortAllocator();
  port_allocator.AddPortsToPoolFromString(absl::GetFlag(FLAGS_ports_to_use));

  distbench::set_use_ipv4_first(absl::GetFlag(FLAGS_use_ipv4_first));
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
        std::cerr << "Initializing the node manager failed: "
                  << status << std::endl;
      }
      node_manager.Wait();
      return !status.ok();
    }
  }

  std::cerr << "Unrecognized distbench module\n";
  Usage();
  return 1;
}
