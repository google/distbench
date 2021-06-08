# Distbench test format

The distbench are tests are defined using Protobuf using the
[dstmf.proto](../dstmf.proto) specification.

This documents decribe the different options available. The format is still
evolving and subject to change.

For examples of tests, a good starting point is the workloads aleady included
with Distbench (see [workloads/README.md](../workloads/README.md)).

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
- `server_type` (string): name of the service.
- `count` (int32): number of instance to start (each instance will occupy a
  `node_manage` unless it is bundled with other services).

### message `ServiceBundle`

- `services` (string, repeated): list of the services (by name) to bundle
  together.
- `constraints` (string, repeated) **Not currently used**

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

- `name` (string): name the rpc.
- `client` (string): The client `service` (initiator of the RPC)
- `server` (string): The server `service` (target of the RPC)
- `request_payload_name` (string): PayloadSpec to use as a payload for the
  request
- `response_payload_name` (string): PayloadSpec to use as a payload for the
  reponse
- `fanout_filter` (string, default=all): select the instance(s) of `server` to
  send the RPC to.
  - `all`: Send the RPC to all the instances of `server`, every time.
  - `random`: Choose a random instance.
  - `round_robin`: Choose one instance in a round-robin fashion.
  - An unrecognized value will target the instance 0 of `server`.
- `tracing_interval` (int32)
  - **0**: Disable tracing
  - **>0**: Create a trace of the RPC in the report every `tracing_interval`
    times (`rpc.id % tracing_interval == 0`).

### message `PayloadSpec`

- `name` (string): name of the PayloadSpec.
- `pool_size` (int32) **Not currently used**
- `template_payload_name` (string) **Not currently used**
- `size` (int32): The size in bytes of the payload
- `seed` (int32) **Not currently used**
- `literal_contents` (repeated bytes) **Not currently used**

