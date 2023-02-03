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

#include <fcntl.h>

#include <fstream>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"
#include "distbench_utils.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

namespace {
bool AreRemainingArgumentsOK(std::vector<char*> remaining_arguments,
                             size_t min_expected, size_t max_expected);
int MainRunTests(std::vector<char*>& arguments);
int MainTestSequencer(std::vector<char*>& arguments);
int MainNodeManager(std::vector<char*>& arguments);
void Usage();
}  // anonymous namespace

ABSL_FLAG(int, port, 10'000, "port to listen on");
ABSL_FLAG(std::string, test_sequencer, "", "host:port of test sequencer");
ABSL_FLAG(bool, binary_output, false, "Save protobufs in binary mode");
ABSL_FLAG(std::string, infile, "/dev/stdin", "Input file");
ABSL_FLAG(std::string, outfile, "/dev/stdout", "Output file");
ABSL_FLAG(std::string, control_plane_netdev, "",
          "netdev to bind to when listening for incoming control connections.");
ABSL_FLAG(int, local_nodes, 0,
          "The number of node managers to run alongside the test sequencer "
          "(primarily for debugging locally)");
ABSL_FLAG(std::string, default_data_plane_device, "",
          "Default netdevice to use for the data plane (protocol driver)");
ABSL_FLAG(absl::Duration, max_test_duration, absl::Hours(0),
          "Maximum time to wait for each test - will default to 1 hour if "
          "not specified by this flag or the test's test_timeout attribute");

int main(int argc, char** argv, char** envp) {
  std::vector<char*> remaining_arguments = absl::ParseCommandLine(argc, argv);
  distbench::InitLibs(argv[0]);

  bool args_ok = AreRemainingArgumentsOK(remaining_arguments, 2,
                                         std::numeric_limits<size_t>::max());
  if (!args_ok) return 1;

  char* distbench_module = remaining_arguments[1];

  // Remove argv[0] and distbench_module
  remaining_arguments.erase(remaining_arguments.begin(),
                            remaining_arguments.begin() + 2);

  if (!strcmp(distbench_module, "test_sequencer")) {
    return MainTestSequencer(remaining_arguments);
  } else if (!strcmp(distbench_module, "node_manager")) {
    return MainNodeManager(remaining_arguments);
  } else if (!strcmp(distbench_module, "run_tests")) {
    return MainRunTests(remaining_arguments);
  } else if (!strcmp(distbench_module, "help")) {
    Usage();
    return 0;
  } else {
    std::cerr << "Unrecognized distbench module: " << distbench_module << "\n";
    Usage();
    return 1;
  }
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
    for (auto it = remaining_arguments.begin() + min_expected;
         it < remaining_arguments.end(); it++) {
      std::cerr << "Error: unexpected command line argument: " << *it << "\n";
    }
    std::cerr << "\n";
    Usage();
    return false;
  }

  return true;
}

// Returns the sum of the specified test_timeout for all tests.
// If even a single test hasn't a test_timeout specified, returns
// timeout_default
absl::StatusOr<int64_t> GetTestSequenceTimeout(
    const distbench::TestSequence& test_sequence, int64_t timeout_default) {
  int64_t accumulated_timeout = 0;
  for (const auto& test : test_sequence.tests()) {
    auto maybe_timeout = GetNamedAttributeInt64(
        test, "test_timeout", std::numeric_limits<int64_t>::min());
    if (!maybe_timeout.ok()) {
      return maybe_timeout;
    }
    if (*maybe_timeout == std::numeric_limits<int64_t>::min()) {
      return timeout_default;
    }
    if (*maybe_timeout <= 0) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid test_timeout specified (", *maybe_timeout,
          "). test_timeout should be a stricly positive integer in seconds."));
    }
    accumulated_timeout += *maybe_timeout;
  }

  if (accumulated_timeout == 0) {
    return timeout_default;
  }

  return accumulated_timeout;
}

void SetAllTestTimeoutAttributesTo(distbench::TestSequence* test_sequence,
                                   std::string value) {
  std::string name = "test_timeout";
  for (auto& test : *test_sequence->mutable_tests()) {
    (*test.mutable_attributes())[name] = value;
  }
}

int MainRunTests(std::vector<char*>& arguments) {
  if (!AreRemainingArgumentsOK(arguments, 0, 0)) return 1;

  const std::string infile = absl::GetFlag(FLAGS_infile);
  auto test_sequence = distbench::ParseTestSequenceProtoFromFile(infile);
  if (!test_sequence.ok()) {
    std::cerr << "Error reading test sequence: " << test_sequence.status()
              << "\n";
    return 1;
  }

  int64_t flag_timeout_seconds =
      ToInt64Seconds(absl::GetFlag(FLAGS_max_test_duration));
  if (flag_timeout_seconds != 0) {
    // The command line flag will override the test specified timeouts.
    SetAllTestTimeoutAttributesTo(&*test_sequence,
                                  absl::StrCat(flag_timeout_seconds));
  } else {
    // 1 hour default if unspecified.
    flag_timeout_seconds = 60 * 60;
  }
  auto maybe_timeout_seconds =
      GetTestSequenceTimeout(*test_sequence, flag_timeout_seconds);
  if (!maybe_timeout_seconds.ok()) {
    std::cerr << "Error in the test sequence: "
              << maybe_timeout_seconds.status() << "\n";
    return 1;
  }

  std::shared_ptr<grpc::ChannelCredentials> client_creds =
      distbench::MakeChannelCredentials();
  std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(
      absl::GetFlag(FLAGS_test_sequencer), client_creds,
      distbench::DistbenchCustomChannelArguments());
  std::unique_ptr<distbench::DistBenchTestSequencer::Stub> stub =
      distbench::DistBenchTestSequencer::NewStub(channel);

  if (!stub) {
    std::cerr << "Error connecting to the TestSequencer\n";
    return 1;
  }

  grpc::ClientContext context;
  distbench::SetGrpcClientContextDeadline(&context, *maybe_timeout_seconds);
  distbench::TestSequenceResults test_results;
  grpc::Status status =
      stub->RunTestSequence(&context, *test_sequence, &test_results);
  if (!status.ok()) {
    std::cerr << "The RunTestSequence RPC Failed with status: " << status
              << "\n";
    if (status.error_message() == "failed to connect to all addresses") {
      std::cerr << "There may be a problem with the test sequencer running on '"
                << absl::GetFlag(FLAGS_test_sequencer)
                << "' the specified host or port may be wrong, or the host it "
                << "is running on may not be reachable from here.\n";
    }
    return 1;
  }

  for (const auto& test_result : test_results.test_results()) {
    std::cout << "Test summary:\n";
    for (const auto& log_summary : test_result.log_summary()) {
      std::cout << log_summary << "\n";
    }
    std::cout << "\n";
  }

  const std::string result_filename = absl::GetFlag(FLAGS_outfile);
  if (!result_filename.empty()) {
    absl::Status save_status;
    if (absl::GetFlag(FLAGS_binary_output)) {
      save_status = SaveResultProtoToFileBinary(result_filename, test_results);
    } else {
      save_status = SaveResultProtoToFile(result_filename, test_results);
    }
    if (!save_status.ok()) {
      std::cerr << "Unable to save the results: " << save_status << "\n";
      return 1;
    }
  }

  return 0;
}

int MainTestSequencer(std::vector<char*>& arguments) {
  if (!AreRemainingArgumentsOK(arguments, 0, 0)) return 1;
  int port = absl::GetFlag(FLAGS_port);
  distbench::TestSequencerOpts opts = {
      .port = &port,
  };
  distbench::TestSequencer test_sequencer;
  test_sequencer.Initialize(opts);
  std::vector<std::unique_ptr<distbench::NodeManager>> nodes;
  int num_nodes = absl::GetFlag(FLAGS_local_nodes);
  nodes.reserve(num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    int new_port = 0;
    const distbench::NodeManagerOpts opts = {
        .test_sequencer_service_address = test_sequencer.service_address(),
        .default_data_plane_device = "",
        .control_plane_device = absl::GetFlag(FLAGS_control_plane_netdev),
        .port = &new_port,
    };
    nodes.push_back(std::make_unique<distbench::NodeManager>());
    absl::Status status = nodes.back()->Initialize(opts);
    if (!status.ok()) {
      std::cerr << "Initializing one of the node managers failed: " << status
                << std::endl;
    }
  }
  test_sequencer.Wait();
  for (int i = 0; i < num_nodes; ++i) {
    nodes[i]->Shutdown();
    nodes[i]->Wait();
  }
  return 0;
}

int MainNodeManager(std::vector<char*>& arguments) {
  if (!AreRemainingArgumentsOK(arguments, 0, 0)) return 1;
  int port = absl::GetFlag(FLAGS_port);
  const distbench::NodeManagerOpts opts = {
      .test_sequencer_service_address = absl::GetFlag(FLAGS_test_sequencer),
      .default_data_plane_device =
          absl::GetFlag(FLAGS_default_data_plane_device),
      .port = &port,
  };
  distbench::NodeManager node_manager;
  absl::Status status = node_manager.Initialize(opts);
  if (!status.ok()) {
    std::cerr << "Initializing the node manager failed: " << status
              << std::endl;
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
  std::cerr << "\n";
  std::cerr << "  distbench node_manager [--test_sequencer=host:port] "
               "[--port=port_number]\n";
  std::cerr << "      --test_sequencer=h:p  The host:port of the "
               "test_sequencer to connect to.\n";
  std::cerr << "      --port=port_number    The port for the "
               "node_manager to listen on.\n";

  std::cerr << "\n";
  std::cerr << "  distbench run_tests "
               "--test_sequencer=host:port "
               "[--infile test_sequence.proto_text] "
               "[--outfile result.proto_text] "
               "[--binary_output]"
               "\n";
  std::cerr << "\n";
  std::cerr << "  distbench help\n";
  std::cerr << "\n";
  std::cerr << "For more options information, do\n";
  std::cerr << "  distbench --helpfull\n";
}
}  // anonymous namespace
