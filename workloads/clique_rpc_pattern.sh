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
DEFAULT_NODE_COUNT=10
NODE_COUNT=$DEFAULT_NODE_COUNT

show_help() {
  echo "Usage: $0 [-h] [-v] [-s hostname:port] [-n val]"
  echo "   Perform the clique RPC pattern (a periodical -every few ms- all-to-all exchange"
  echo "   of small messages)."
  echo
  echo "   -h               Display the usage help (this)"
  echo "   -s hostname:port Connect to the test sequencer located at hostname:port"
  echo "                      default: $DEFAULT_SEQUENCER"
  echo "   -n val           Indicate the number (val) of nodes (clique services) to run"
  echo "                      each service requires a node_manager"
  echo "                      default: $DEFAULT_NODE_COUNT, minimum 2"
  echo
}

while getopts "h?vs:n:" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    v)  VERBOSE=1
        ;;
    s)  SEQUENCER=$OPTARG
        ;;
    n)  NODE_COUNT=$OPTARG
        ;;
    esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift

if [ "$NODE_COUNT" -le "1" ]; then
  echo "ERROR: NODE_COUNT is less than 2"
  show_help
  exit 1
fi

if [[ "${VERBOSE}" = "1" ]]; then
  echo Running the Clique RPC pattern
  echo "  VERBOSE=$VERBOSE"
  echo "  SEQUENCER=$SEQUENCER"
  echo "  NODE_COUNT=$NODE_COUNT"
fi

grpc_cli \
  --channel_creds_type=insecure \
  call $SEQUENCER \
  distbench.DistBenchTestSequencer.RunTestSequence \
<<EOF
tests {
  services {
    server_type: "clique"
    count: $NODE_COUNT
  }
  action_lists {
    name: "clique"
    action_names: "clique_queries"
  }
  action_table {
    name: "clique_queries"
    iterations {
      max_duration_us: 10000000
      open_loop_interval_ns: 16000000
      open_loop_interval_distribution: "sync_burst"
    }
    rpc_name: "clique_query"
  }
  rpc_descriptions {
    name: "clique_query"
    client: "clique"
    server: "clique"
    fanout_filter: "all"
    tracing_interval: 2
  }
  action_lists {
    name: "clique_query"
    # no actions, NOP
  }
}
EOF
