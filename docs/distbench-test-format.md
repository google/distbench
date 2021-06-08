# Distbench test format

The distbench are tests are defined using Protobuf using the
[dstmf.proto](../dstmf.proto) specification.

This documents decribe the different options available. The format is still
evolving and subject to change.

For examples of tests, a good starting point is the workloads aleady included
with Distbench (see [workloads/README.md](../workloads/README.md).

## Distributed System Description

The test is defined by a `DistributedSystemDescription` message with the
following entities, each of this entity will be described in more detail later
in this document.

- `services`: describe the services and the number of instances to
  create. Each instance will be placed on a Distbench `node_manager`.
- `node_service_bundles`: a map of services to bundle; each bundle will share a
  Distbench `node_manager`.
- `action_list_table`: define a list of action to execute.
- `action_table`: define a action to execute such as running a RPC or calling
  another `action_list_table` repeatively.
- `rpc_descriptions`: describe a RPC to perform, including the type of payload
  and fanout involved.
- `payload_descriptions`: define a payload that can be associated with an RPC.

### message `ServiceSpec`
- `server_type` (string)
- `count` (int32)

### message `ServiceBundle`

- `services` (string, repeated)
- `constraints` (string, repeated)

### message `ActionList` (`action_table`)

- `name` (string): name the actionlist. If the name match a service, the action
  list will be automatically executed by the service.
- `action_names` (string, repeated): define the list of actions to run.
- `serialize_all_actions` (bool): define if the action need to be serialized.
  **Not currently used**

### message `Action` (`action_table`)

- `name` (string): name the action.
- `dependencies` (string, repeated): define a dependency on another action. This
  action will wait until the action specified is complete.
- `iterations` (Iteration): optinally define iterations (see next section)
- `action`: Define the action to execute, as one of the following:
  - `rpc_name`: run the RPC (define in a `rpc_descriptions`).
  - `action_list_table`: run another ActionList (defined by an `action_table`)

### message `Iteration`

- `max_iteration_count` (int32):
- `max_duration_us` (int32):
- `max_parallel_iterations` (int64, default=1):
- `open_loop_interval_ns` (int64):
- `open_loop_interval_distribution` (string, default=constant):

### message `RpcSpec`

- `name` (string)
- `client` (string)
- `server` (string)
- `request_payload_name` (string)
- `response_payload_name` (string)
- `fanout_filter` (string, default=all)
  - `all`:
  - `random`:
  - `round_robin`:
  - Unrecognized value targets the instance 0 of the destination
- `tracing_interval` (int32)

### message `PayloadSpec`

- `name` (string)
- `pool_size` (int32)
- `template_payload_name` (string)
- `size` (int32)
- `seed` (int32)
- `literal_contents` (repeated bytes)

