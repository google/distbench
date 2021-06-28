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

DEFAULT_SERVER_COUNT=5
SERVER_COUNT=$DEFAULT_SERVER_COUNT

show_help() {
  echo "Usage: $0 [-h] [-v] [-s hostname:port] [-c client_cnt] [-i server_cnt]"
  echo "   Run the stochastic test RPC pattern"
  echo
  echo "   -h               Display the usage help (this)"
  echo "   -s hostname:port Connect to the test sequencer located at hostname:port"
  echo "                      default: $DEFAULT_SEQUENCER"
  echo "   -c client_cnt     Indicate the number of client nodes"
  echo "                      default: $DEFAULT_CLIENT_COUNT"
  echo "   -i server_cnt     Indicate the number of server nodes"
  echo "                      default: $DEFAULT_SERVER_COUNT"
  echo "   Note: you will need server_cnt+client_cnt node managers"
  echo
}

while getopts "h?vs:c:i:" opt; do
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
    esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift

if [[ "${VERBOSE}" = "1" ]]; then
  echo Running the stochastic test RPC pattern
  echo "  VERBOSE=$VERBOSE"
  echo "  SEQUENCER=$SEQUENCER"
  echo "  CLIENT_COUNT=$CLIENT_COUNT"
  echo "  SERVER_COUNT=$SERVER_COUNT"
  echo
fi

grpc_cli \
  --channel_creds_type=insecure \
  call $SEQUENCER \
  distbench.DistBenchTestSequencer.RunTestSequence \
<<EOF
tests {
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
    fanout_filter: "stochastic{0.7:1,0.3:4}"
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
      max_iteration_count: 100
    }
  }
  action_lists {
    name: "client_server_rpc"
    # No action on the server; just send the response
  }
}
EOF
