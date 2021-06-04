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

DEFAULT_INDEX_COUNT=1
INDEX_COUNT=$DEFAULT_INDEX_COUNT

DEFAULT_RESULT_COUNT=1
RESULT_COUNT=$DEFAULT_RESULT_COUNT

show_help() {
  echo "Usage: $0 [-h] [-v] [-s hostname:port] [-c client_cnt] [-r index_cnt] [-l result_cnt]"
  echo "   Run the tripartite RPC pattern"
  echo
  echo "   -h               Display the usage help (this)"
  echo "   -s hostname:port Connect to the test sequencer located at hostname:port"
  echo "                      default: $DEFAULT_SEQUENCER"
  echo "   -c client_cnt     Indicate the number of client nodes"
  echo "                      default: $DEFAULT_CLIENT_COUNT"
  echo "   -i index_cnt     Indicate the number of index nodes"
  echo "                      default: $DEFAULT_INDEX_COUNT"
  echo "   -l result_cnt    Indicate the number of result nodes"
  echo "                      default: $DEFAULT_RESULT_COUNT"
  echo "   Note: you will need index_cnt+result_cnt+client_cnt node managers"
  echo
}

while getopts "h?vs:c:i:l:" opt; do
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
    i)  INDEX_COUNT=$OPTARG
        ;;
    r)  RESULT_COUNT=$OPTARG
        ;;
    esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift

if [[ "${VERBOSE}" = "1" ]]; then
  echo Running the tripartite RPC pattern
  echo "  VERBOSE=$VERBOSE"
  echo "  SEQUENCER=$SEQUENCER"
  echo "  CLIENT_COUNT=$CLIENT_COUNT"
  echo "  INDEX_COUNT=$INDEX_COUNT"
  echo "  RESULT_COUNT=$RESULT_COUNT"
  echo
fi

grpc_cli \
  --channel_creds_type=insecure \
  call $SEQUENCER \
  distbench.DistBenchTestSequencer.RunTestSequence \
<<EOF
tests {
  services {
    server_type: "client"
    count: $CLIENT_COUNT
  }
  services {
    server_type: "index"
    count: $INDEX_COUNT
  }
  services {
    server_type: "result"
    count: $RESULT_COUNT
  }
  rpc_descriptions {
    name: "client_index_rpc"
    client: "client"
    server: "index"
    request_payload_name: "request_payload"
    response_payload_name: "response_payload"
  }
  rpc_descriptions {
    name: "client_result_rpc"
    client: "client"
    server: "index"   # TODO: change to result when issue is fixed
    request_payload_name: "request_result_payload"
    response_payload_name: "response_result_payload"
  }
  payload_descriptions {
    name: "request_payload"
    size: 196
  }
  payload_descriptions {
    name: "response_payload"
    size: 1024
  }
  action_list_table {
    name: "client"
    action_names: "client_do_many_queries"
  }
  action_table {
    name: "client_do_many_queries"
    iterations {
      max_iteration_count: 10
    }
    action_list_name: "client_do_one_query"
  }
  action_list_table {
    name: "client_do_one_query"
    action_names: "client_queryindex"
    action_names: "client_queryresult"
  }
  action_table {
    name: "client_queryindex"
    rpc_name: "client_index_rpc"
  }
  action_list_table {
    name: "client_index_rpc"
    # No action on the client; just send the response
  }
  action_list_table {
    name: "client_result_rpc"
    # No action on the client; just send the response
  }
  action_table {
    name: "client_queryresult"
    rpc_name: "client_result_rpc"
    dependencies: "client_queryindex"
  }
  payload_descriptions {
    name: "request_result_payload"
    size: 64
  }
  payload_descriptions {
    name: "response_result_payload"
    size: 512
  }
}
EOF
