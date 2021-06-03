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

# Test variables
VERBOSE=0
DEFAULT_SEQUENCER=localhost:10000
SEQUENCER=$DEFAULT_SEQUENCER
DEFAULT_ROOT_COUNT=1
ROOT_COUNT=$DEFAULT_ROOT_COUNT
DEFAULT_LEAF_COUNT=3
LEAF_COUNT=$DEFAULT_LEAF_COUNT

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
  echo
  echo "   Note: you will need root_cnt+leaf_cnt+1 node managers"
  echo
}

OPTIND=1

while getopts "h?vs:r:l:" opt; do
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
fi

grpc_cli \
  --channel_creds_type=insecure \
  call $SEQUENCER \
  distbench.DistBenchTestSequencer.RunTestSequence \
<<EOF
tests {
  services {
    server_type: "load_balancer"
    count: 1
  }
  services {
    server_type: "root"
    count: $ROOT_COUNT
  }
  services {
    server_type: "leaf"
    count: $LEAF_COUNT
  }
  action_list_table {
    name: "load_balancer"
    action_names: "load_balancer/do_closed_loop_root_queries"
    action_names: "load_balancer/do_closed_loop_root_queries_again"
  }
  action_table {
    name: "load_balancer/do_closed_loop_root_queries"
    iterations {
      max_iteration_count: 3
      max_parallel_iterations: 3
    }
    rpc_name: "root_query"
  }
  action_table {
    name: "load_balancer/do_closed_loop_root_queries_again"
    dependencies : "load_balancer/do_closed_loop_root_queries"
    iterations {
      #max_iteration_count: 3
      max_duration_us: 3000000
      open_loop_interval_ns: 1000000000
      open_loop_interval_distribution: "sync_burst"
    }
    action_list_name: "load_balancer/root_query_al"
  }
  action_list_table {
    name: "load_balancer/root_query_al"
    action_names: "load_balancer/do_closed_loop_root_queries"
    #action_names: "root/root_query_fanout"
  }
  rpc_descriptions {
    name: "root_query"
    client: "load_balancer"
    server: "root"
    fanout_filter: "round_robin"
    tracing_interval: 2
  }
  action_list_table {
    name: "root_query"
    action_names: "root/root_query_fanout"
    # Should be able to use an action name in-place of a single entry action list?
  }
  action_table {
    name: "root/root_query_fanout"
    rpc_name: "leaf_query"
  }
  rpc_descriptions {
    name: "leaf_query"
    client: "root"
    server: "leaf"
    fanout_filter: "all"
    tracing_interval: 2
  }
  action_list_table {
    name: "leaf_query"
    # no actions, NOP
  }
}
EOF
