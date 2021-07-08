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

namespace {
bool AreRemainingArgumentsOK(std::vector<char*> remaining_arguments,
                             size_t min_expected, size_t max_expected);
int MainTestSequencer(std::vector<char*> &arguments);
int MainNodeManager(std::vector<char*> &arguments);
void Usage();
}  // anonymous namespace

ABSL_FLAG(int, port, 10000, "port to listen on");
ABSL_FLAG(std::string, test_sequencer, "", "host:port of test sequencer");
ABSL_FLAG(bool, use_ipv4_first, false,
    "Prefer IPv4 addresses to IPv6 addresses when both are available");

int main(int argc, char** argv, char** envp) {
  std::vector<char*> remaining_arguments = absl::ParseCommandLine(argc, argv);
  distbench::InitLibs(argv[0]);
  distbench::set_use_ipv4_first(absl::GetFlag(FLAGS_use_ipv4_first));

  if (!AreRemainingArgumentsOK(remaining_arguments,
                               2, std::numeric_limits<size_t>::max()))
    return 1;

  char *distbench_module = remaining_arguments[1];

  // Remove argv[0] and distbench_module
  remaining_arguments.erase(remaining_arguments.begin(),
                            remaining_arguments.begin() + 2);

  if (!strcmp(distbench_module, "test_sequencer")) {
    return MainTestSequencer(remaining_arguments);
  } else if (!strcmp(distbench_module, "node_manager")) {
    return MainNodeManager(remaining_arguments);
  }

  std::cerr << "Unrecognized distbench module: " << distbench_module << "\n";
  Usage();
  return 1;
}

namespace {
bool AreRemainingArgumentsOK(std::vector<char*> remaining_arguments,
                             size_t min_expected, size_t max_expected) {
  size_t nb_arguments = remaining_arguments.size();
  if (nb_arguments < min_expected) {
    std::cerr << "Not enough arguments provided\n";
    Usage();
    return false;
  }

  if (nb_arguments > max_expected) {
    for (auto it=remaining_arguments.begin() + min_expected;
         it < remaining_arguments.end(); it++) {
      std::cerr << "Error: unexpected command line argument: " << *it << "\n";
    }
    std::cerr << "\n";
    Usage();
    return false;
  }

  return true;
}

int MainTestSequencer(std::vector<char*> &arguments) {
  if (!AreRemainingArgumentsOK(arguments, 0, 0))
    return 1;
  distbench::TestSequencerOpts opts = {};
  int port = absl::GetFlag(FLAGS_port);
  opts.port = &port;
  distbench::TestSequencer test_sequencer;
  test_sequencer.Initialize(opts);
  test_sequencer.Wait();
  return 0;
}

int MainNodeManager(std::vector<char*> &arguments) {
  if (!AreRemainingArgumentsOK(arguments, 0, 0))
    return 1;
  distbench::NodeManagerOpts opts = {};
  opts.test_sequencer_service_address = absl::GetFlag(FLAGS_test_sequencer);
  int port = absl::GetFlag(FLAGS_port);
  opts.port = &port;
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
}  // anonymous namespace
