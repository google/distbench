#!/bin/bash
set -eu
rm -rf test_builder_golden_configs
mkdir test_builder_golden_configs -p
cd test_builder_golden_configs
PATH+=":.."

! make -C "$(bazel info output_base)/external/homa_module/util" dist_to_proto
PATH="$PATH:$(bazel info output_base)/external/homa_module/util/"

# Generate a config for each flag of each traffic pattern and protocol driver:
test_builder -o . clique
test_builder -o . clique:test_duration=45

test_builder -o . clique:grpc
test_builder -o . clique:grpc:transport=homa
test_builder -o . clique:grpc:transport=tcp

test_builder -o . clique:grpc:client_type=callback:server_type=handoff
test_builder -o . clique:grpc:client_type=callback:server_type=inline
test_builder -o . clique:grpc:client_type=polling:server_type=handoff

test_builder -o . clique:rpc_interval_us=2000
test_builder -o . clique:node_count=123
test_builder -o . clique:synchronization_mode=sync_burst_spread
test_builder -o . clique:request_size=16384
test_builder -o . clique:response_size=0

test_builder -o . rectangular
test_builder -o . rectangular:rpc_interval_us=1000000
test_builder -o . rectangular:x_size=4
test_builder -o . rectangular:y_size=2
test_builder -o . rectangular:synchronization_mode=exponential
test_builder -o . rectangular:request_size=16384
test_builder -o . rectangular:response_size=0
test_builder -o . rectangular:fanout_filter=same_y

test_builder -o . client_server
test_builder -o . client_server:parallel_queries=25
test_builder -o . client_server:client_count=3
test_builder -o . client_server:server_count=6
test_builder -o . client_server:server_type=handoff:threadpool_size=8

test_builder -o . multi_level_rpc
test_builder -o . multi_level_rpc:qps=20000
test_builder -o . multi_level_rpc:root_count=6
test_builder -o . multi_level_rpc:leaf_count=90

test_builder -o . tripartite
test_builder -o . tripartite:client_count=2
test_builder -o . tripartite:index_count=2
test_builder -o . tripartite:data_count=12

test_builder -o . clique:grpc:ipv4
test_builder -o . clique:grpc:ipv6

test_builder -o . clique:homa
test_builder -o . clique:homa:ipv4
test_builder -o . clique:homa:ipv6
test_builder -o . clique:homa:threadpool_type=simple
test_builder -o . clique:homa:threadpool_size=2
test_builder -o . clique:homa:ping_pong=0
test_builder -o . clique:homa:ping_pong=1
test_builder -o . clique:homa:client_threads=2
test_builder -o . clique:homa:server_threads=3
test_builder -o . clique:homa:nocopy=0
test_builder -o . clique:homa:nocopy=1

test_builder -o . clique:mercury
test_builder -o . clique:mercury:ipv4
test_builder -o . clique:mercury:ipv6
test_builder -o . clique:mercury
test_builder -o . clique:mercury:transport=ofi+tcp
test_builder -o . clique:mercury:transport=custom_transport
test_builder -o . clique:mercury:threadpool_size=16
test_builder -o . clique:mercury:threadpool_type=elastic

test_builder -o . variable_response_multi_level_rpc
test_builder -o . variable_response_multi_level_rpc:qps=5000
test_builder -o . variable_response_multi_level_rpc:root_size=5
test_builder -o . variable_response_multi_level_rpc:leaf_size=34
test_builder -o . variable_response_multi_level_rpc:exact_size=35000
test_builder -o . variable_response_multi_level_rpc:lower_size=1000
test_builder -o . variable_response_multi_level_rpc:upper_size=65000

test_builder -o . homa_cp_node
test_builder -o . homa_cp_node:homa
test_builder -o . homa_cp_node:node_count=5
test_builder -o . homa_cp_node:gbps=2
test_builder -o . homa_cp_node:synchronization_mode=constant
test_builder -o . homa_cp_node:workload=w4
test_builder -o . homa_cp_node:max_message_length=4000000
test_builder -o . homa_cp_node:min_bucket_frac=0.1
test_builder -o . homa_cp_node:max_size_ratio=1.05

# Generate a parameter sweep:
test_builder -o - parameter_sweep rpc_interval_us 4000 1000 8000 clique \
  > sweep1.config

# Generate a multi-config parameter sweep:
test_builder -o - parameter_sweep rpc_interval_us 4000 1000 8000 \
  -c clique -l LabelFoo- \
  node_count=2 node_count=3 node_count=4 \
  > sweep2.config

# Generate a parameter sweep of threadpool_size:
test_builder -o - parameter_sweep threadpool_size 1 1 4 client_server:server_type=polling \
  > sweep3.config
