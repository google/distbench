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

#include <fcntl.h>

#include <fstream>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

namespace {
bool AreRemainingArgumentsOK(std::vector<char*> remaining_arguments,
                             size_t min_expected, size_t max_expected);
int MainRunTests(std::vector<char*> &arguments);
int MainTestSequencer(std::vector<char*> &arguments);
int MainNodeManager(std::vector<char*> &arguments);
void Usage();
}  // anonymous namespace

ABSL_FLAG(int, port, 10000, "port to listen on");
ABSL_FLAG(std::string, test_sequencer, "", "host:port of test sequencer");
ABSL_FLAG(bool, use_ipv4_first, false,
    "Prefer IPv4 addresses to IPv6 addresses when both are available");
ABSL_FLAG(bool, save_binary_protobuf, false,
    "Save protobufs in binary mode");

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
  } else if (!strcmp(distbench_module, "run_tests")) {
    return MainRunTests(remaining_arguments);
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

absl::Status ParseTestSequenceProtoFromFile(
    char *filename,
    distbench::TestSequence *test_sequence) {
  int fd_proto = STDIN_FILENO;
  if (strcmp(filename, "-") != 0)
    fd_proto = open(filename, O_RDONLY);
  if (fd_proto < 0) {
    std::string error_message{"Error opening the TestSequence proto file for "
      "reading: "};
    return absl::InvalidArgumentError(error_message + filename);
  }

  google::protobuf::io::FileInputStream fis_testproto(fd_proto);
  if (fd_proto != STDIN_FILENO)
    fis_testproto.SetCloseOnDelete(true);
  if (!google::protobuf::TextFormat::Parse(&fis_testproto, test_sequence)) {
    return absl::InvalidArgumentError(
        "Error parsing the TestSequence proto file");
  }

  return absl::OkStatus();
}

absl::Status SaveResultProtoToFile(char *filename,
                              const distbench::TestSequenceResults &result) {
  int fd_proto = STDOUT_FILENO;
  if (strcmp(filename, "-") != 0)
    fd_proto = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd_proto < 0) {
    std::string error_message{"Error opening the output result proto file for "
      "writing: "};
    return absl::InvalidArgumentError(error_message + filename);
  }

  google::protobuf::io::FileOutputStream fos_resultproto(fd_proto);
  if (fd_proto != STDOUT_FILENO)
    fos_resultproto.SetCloseOnDelete(true);
  if (!google::protobuf::TextFormat::Print(result, &fos_resultproto)) {
    return absl::InvalidArgumentError(
        "Error writing the result proto file");
  }

  return absl::OkStatus();
}

absl::Status SaveResultProtoToFileBinary(char *filename,
                              const distbench::TestSequenceResults &result) {
  std::fstream output(filename, std::ios::out | std::ios::trunc |
                                std::ios::binary);
  if (!result.SerializeToOstream(&output)) {
    return absl::InvalidArgumentError(
        "Error writing the result proto file in binary mode");
  }

  return absl::OkStatus();
}

int MainRunTests(std::vector<char*> &arguments) {
  // arguments: test_sequence.proto_text [result.proto_text]
  if (!AreRemainingArgumentsOK(arguments, 1, 2))
    return 1;

  distbench::TestSequence test_sequence;
  absl::Status parse_status = ParseTestSequenceProtoFromFile(arguments[0],
                                                             &test_sequence);
  if (!parse_status.ok())
    return 1;

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
  distbench::TestSequenceResults test_results;
  grpc::Status status = stub->RunTestSequence(&context, test_sequence,
                                              &test_results);
  if (status.ok()) {
    for (const auto& test_result: test_results.test_results()) {
      std::cout << "Test summary:\n";
      for (const auto& log_summary: test_result.log_summary()) {
        std::cout << log_summary << "\n";
      }
      std::cout << "\n";
    }
  } else {
    std::cerr << "Failed! " << status << "\n";
  }

  if (arguments.size() == 2) {
    char *result_filename = arguments[1];
    absl::Status save_status;
    if (absl::GetFlag(FLAGS_save_binary_protobuf)) {
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

  std::cerr << "  distbench run_tests test_sequence.proto_text "
                    "[result.proto_text] [--test_sequencer=host:port]\n";
  std::cerr << "\n";
  std::cerr << "For more options information, do\n";
  std::cerr << "  distbench --helpfull\n";
}
}  // anonymous namespace
