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

set -u

# Test variables
VERBOSE=0
DEFAULT_SEQUENCER=localhost:10000
SEQUENCER=$DEFAULT_SEQUENCER
DEFAULT_ROOT_COUNT=1
ROOT_COUNT=$DEFAULT_ROOT_COUNT
DEFAULT_LEAF_COUNT=3
LEAF_COUNT=$DEFAULT_LEAF_COUNT
DEFAULT_PROTOCOL_DRIVER=grpc
PROTOCOL_DRIVER=$DEFAULT_PROTOCOL_DRIVER
DEFAULT_OUTPUT_FILE=""
OUTPUT_FILE=$DEFAULT_OUTPUT_FILE
TIME_SECONDS=${TIME_SECONDS:-30}
DEFAULT_QPS=5000
QPS=${QPS:-$DEFAULT_QPS}

DISTBENCH_BIN=distbench
which $DISTBENCH_BIN || DISTBENCH_BIN=../bazel-bin/distbench

show_help() {
  echo "Usage: $0 [-h] [-v] [-s hostname:port] [-r root_cnt] [-l leaf_cnt]"
  echo "   Running the multi-level RPC tree pattern"
  echo
  echo "   -h               Display the usage help (this)"
  echo "   -s hostname:port Connect to the test sequencer located at hostname:port"
  echo "                      default: $DEFAULT_SEQUENCER"
  echo "   -r root_cnt      Indicate the number (root_cnt) of root nodes"
  echo "                      default: $DEFAULT_ROOT_COUNT"
  echo "   -l leaf_cnt      Indicate the number (leaf_cnt) of leaf nodes"
  echo "                      default: $DEFAULT_LEAF_COUNT"
  echo "   -p protocol_drv  Protocol driver to use"
  echo "                      default: $DEFAULT_PROTOCOL_DRIVER"
  echo "   -o output_file   Filename used to output the result protobuf"
  echo "                      default: $DEFAULT_OUTPUT_FILE"
  echo "   -t runtime_sec   Specify the test run time (e.g. -t 60 for 60secs)"
  echo "   -q qps           Number of QPS to send (default: $DEFAULT_QPS"
  echo
  echo "   Note: you will need root_cnt+leaf_cnt+1 node managers"
  echo
}

OPTIND=1

while getopts "h?vs:r:l:p:o:t:q:" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    v)  VERBOSE=1
        ;;
    s)  SEQUENCER=$OPTARG
        ;;
    r)  ROOT_COUNT=$OPTARG
        ;;
    l)  LEAF_COUNT=$OPTARG
        ;;
    p)  PROTOCOL_DRIVER=$OPTARG
        ;;
    o)  OUTPUT_FILE=$OPTARG
        ;;
    t)  TIME_SECONDS=$OPTARG
        ;;
    q)  QPS=$OPTARG
        ;;
    esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift

if [[ "${VERBOSE}" = "1" ]]; then
  echo Running the multi-level RPC tree pattern
  echo "  VERBOSE=$VERBOSE"
  echo "  SEQUENCER=$SEQUENCER"
  echo "  ROOT_COUNT=$ROOT_COUNT"
  echo "  LEAF_COUNT=$LEAF_COUNT"
  echo "  PROTOCOL_DRIVER=$PROTOCOL_DRIVER"
  echo "  OUTPUT_FILE=$OUTPUT_FILE"
  echo "  TIME_SECONDS=$TIME_SECONDS"
  echo "  QPS=$QPS"
fi

echo The test will run for about $TIME_SECONDS seconds

$DISTBENCH_BIN run_tests --test_sequencer=$SEQUENCER \
                         --outfile="$OUTPUT_FILE" \
                         --binary_output \
                         --max_test_duration=$(( TIME_SECONDS + 100 ))s \
<<EOF
tests {
  name: "multi_level_rpc;qps=$QPS;proto=$PROTOCOL_DRIVER;scale=$LEAF_COUNT"
  default_protocol: "$PROTOCOL_DRIVER"
  services {
    name: "load_balancer"
    count: 1
  }
  services {
    name: "root"
    count: $ROOT_COUNT
  }
  services {
    name: "leaf"
    count: $LEAF_COUNT
  }
  action_lists {
    name: "load_balancer"
    action_names: "load_balancer/do_closed_loop_root_queries"
  }
  actions {
    name: "load_balancer/do_closed_loop_root_queries"
    iterations {
      max_duration_us: ${TIME_SECONDS}000000
      # max_parallel_iterations: 100
      open_loop_interval_ns: $(( 1000000000 / QPS ))
    }
    rpc_name: "root_query"
  }
  rpc_descriptions {
    name: "root_query"
    client: "load_balancer"
    server: "root"
    fanout_filter: "round_robin"
    request_payload_name: "root_request_payload"
    response_payload_name: "root_response_payload"
  }
  action_lists {
    name: "root_query"
    action_names: "root/root_query_fanout"
    max_rpc_samples: 256
    # Should be able to use an action name in-place of a single entry action list?
  }
  actions {
    name: "root/root_query_fanout"
    rpc_name: "leaf_query"
  }
  rpc_descriptions {
    name: "leaf_query"
    client: "root"
    server: "leaf"
    fanout_filter: "all"
    request_payload_name: "leaf_request_payload"
    response_payload_name: "leaf_response_payload"
  }
  action_lists {
    name: "leaf_query"
    # no actions, NOP
  }
  payload_descriptions {
    name: "root_request_payload"
    size: 1024
  }
  payload_descriptions {
    name: "root_response_payload"
    size: 1024
  }
  payload_descriptions {
    name: "leaf_request_payload"
    size: 1024
  }
  payload_descriptions {
    name: "leaf_response_payload"
    size: 1024
  }
}
EOF
