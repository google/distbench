#!/bin/bash

# Copyright 2023 Google LLC
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


# This is a simple test, doing a search like pattern but with all the services
# located on a single node.

# This script require that the test sequencer and node manager be started on
# localhost prior to invoking the RPC below. You can either do it manually as
# shown below or by using the start_distbench_localhost.sh script.
#
# To start the test_sequencer run the following in a dedicated terminal:
# bazel run :distbench -- test_sequencer
# To start the node manager run the following in a dedicated terminal:
# bazel run :distbench -- node_manager --test_sequencer=localhost:10000 --port=9999

bazel run :distbench -c opt -- run_tests --test_sequencer=localhost:10000 \
  --outfile "" \
<<EOF
tests {
  services {
    name: "load_balancer"
    count: 1
  }
  services {
    name: "root"
    count: 1
  }
  services {
    name: "leaf"
    count: 3
  }
  node_service_bundles {
    key: "node0"
    value: {
      services: "load_balancer/0"
      services: "root/0"
      services: "leaf/0"
      services: "leaf/1"
      services: "leaf/2"
    }
  }
  action_lists {
    name: "load_balancer"
    action_names: "load_balancer/do_closed_loop_root_queries"
    action_names: "load_balancer/do_closed_loop_root_queries_again"
  }
  actions {
    name: "load_balancer/do_closed_loop_root_queries"
    iterations {
      max_iteration_count: 3
      max_parallel_iterations: 3
    }
    rpc_name: "root_query"
  }
  actions {
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
  action_lists {
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
  action_lists {
    name: "root_query"
    action_names: "root/root_query_fanout"
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
    tracing_interval: 2
  }
  action_lists {
    name: "leaf_query"
    # no actions, NOP
  }
}
EOF
