#!/bin/bash

# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

OPTIND=1

# Test variables
VERBOSE=0
DEFAULT_SEQUENCER=localhost:10000
SEQUENCER=$DEFAULT_SEQUENCER

DEFAULT_CLIENT_COUNT=1
CLIENT_COUNT=$DEFAULT_CLIENT_COUNT

DEFAULT_SERVER_COUNT=1
SERVER_COUNT=$DEFAULT_SERVER_COUNT

DEFAULT_PROTOCOL_DRIVER=grpc
PROTOCOL_DRIVER=$DEFAULT_PROTOCOL_DRIVER
DEFAULT_OUTPUT_FILE=""
OUTPUT_FILE=$DEFAULT_OUTPUT_FILE
TIME_SECONDS=${TIME_SECONDS:-30}

DISTBENCH_BIN=distbench
which $DISTBENCH_BIN || DISTBENCH_BIN=../bazel-bin/distbench

show_help() {
  echo "Usage: $0 [-h] [-v] [-s hostname:port] [-c client_cnt] [-i server_cnt]"
  echo "   Run the client server RPC pattern"
  echo
  echo "   -h               Display the usage help (this)"
  echo "   -s hostname:port Connect to the test sequencer located at hostname:port"
  echo "                      default: $DEFAULT_SEQUENCER"
  echo "   -c client_cnt     Indicate the number of client nodes"
  echo "                      default: $DEFAULT_CLIENT_COUNT"
  echo "   -i server_cnt     Indicate the number of server nodes"
  echo "                      default: $DEFAULT_SERVER_COUNT"
  echo "   Note: you will need server_cnt+client_cnt node managers"
  echo "   -p protocol_drv  Protocol driver to use"
  echo "                      default: $DEFAULT_PROTOCOL_DRIVER"
  echo "   -o output_file   Filename used to output the result protobuf"
  echo "                      default: $DEFAULT_OUTPUT_FILE"
  echo "   -t runtime_sec   Specify the test run time (e.g. -t 60 for 60secs)"
  echo
}

while getopts "h?vs:c:i:p:o:t:" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    v)  VERBOSE=1
        ;;
    s)  SEQUENCER=$OPTARG
        ;;
    c)  CLIENT_COUNT=$OPTARG
        ;;
    i)  SERVER_COUNT=$OPTARG
        ;;
    p)  PROTOCOL_DRIVER=$OPTARG
        ;;
    o)  OUTPUT_FILE=$OPTARG
        ;;
    t)  TIME_SECONDS=$OPTARG
        ;;
    esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift

if [[ "${VERBOSE}" = "1" ]]; then
  echo Running the client server RPC pattern
  echo "  VERBOSE=$VERBOSE"
  echo "  SEQUENCER=$SEQUENCER"
  echo "  CLIENT_COUNT=$CLIENT_COUNT"
  echo "  SERVER_COUNT=$SERVER_COUNT"
  echo "  PROTOCOL_DRIVER=$PROTOCOL_DRIVER"
  echo "  OUTPUT_FILE=$OUTPUT_FILE"
  echo "  TIME_SECONDS=$TIME_SECONDS"
  echo
fi

$DISTBENCH_BIN run_tests --test_sequencer=$SEQUENCER \
                         --outfile="$OUTPUT_FILE" \
                         --binary_output \
                         --max_test_duration=$(( TIME_SECONDS + 30 ))s \
<<EOF
tests {
  default_protocol: "$PROTOCOL_DRIVER"
  services {
    name: "client"
    count: $CLIENT_COUNT
  }
  services {
    name: "server"
    count: $SERVER_COUNT
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
    request_payload_name: "request_payload"
    response_payload_name: "response_payload"
  }
  payload_descriptions {
    name: "request_payload"
    size: 196
  }
  payload_descriptions {
    name: "response_payload"
    size: 262144
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_duration_us: ${TIME_SECONDS}000000
      max_parallel_iterations: 100
    }
  }
  action_lists {
    name: "client_server_rpc"
    # No action on the server; just send the response
  }
}
EOF
