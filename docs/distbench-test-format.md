# Distbench Traffic Pattern - Test Format

Distbench tests are specified in protocol buffers that are defined
 in the [traffic_config.proto](../traffic_config.proto).

This document decribes the different options available. The format is still
evolving and subject to change.

For examples of tests, a good starting point is the workloads already included
with Distbench (see [workloads/README.md](../workloads/README.md)).

For pointers to other Distbench Documents, see the
[Distbench README](../README.md).

## Distributed System Description

The test is defined by a `DistributedSystemDescription` message with the
following entities, each of this entity will be described in more detail later
in this document.

- `services`: describe the services and the number of instances to
  create. Each instance will be placed on a Distbench `node_manager`.
- `node_service_bundles`: a map of services to bundle; each bundle will share a
  Distbench `node_manager`.
- `action_lists`: define a list of action to execute.
- `actions`: define a action to execute such as running a RPC or calling
  another `action_lists` repetitively.
- `rpc_descriptions`: describe a RPC to perform, including the type of payload
  and fanout involved.
- `payload_descriptions`: define a payload that can be associated with an RPC.
- `attributes`:
- `distribution_config` : describe a distribution for random RPC `payload_size`,
 `request_size`, or `response_size`.
  - `test_timeout`: Maximum time to run the test in seconds.

**Note:** by convention, repeated fields in the proto are described by plural
names. So a `services` block describes a single service, but there may be
multiple of them.

### message `ServiceSpec`
- `name` (string): name of the service.
- `count` (int32): number of instances to start (each instance will occupy a
  `node_manage` unless it is bundled with other services).
- `protocol_driver_options_name`: name of the ProtocolDriverOptions to use for
  the service.

### message `ServiceBundle`

- `services` (string, repeated): list of the services (by name) to bundle
  together.

### message `ActionList` (`actions`)

- `name` (string): name of the ActionList. If the name match a service, the action
  list will be automatically executed by the service.
- `action_names` (string, repeated): define the list of actions to run.

Note: the actions specified are run in no specific order, unless a
`dependencies` is specified in the `Action` itself.

### message `Action` (`actions`)

- `name` (string): name the action.
- `dependencies` (string, repeated): define a dependency on another action. This
  action will wait until the action specified is complete.
- `iterations` (Iteration): optionally define iterations (see next section)
- `action`: Define the action to execute, as one of the following:
  - `rpc_name`: run the RPC (defined in a `rpc_descriptions`).
  - `action_lists`: run another ActionList (defined by an `actions`)

### message `Iteration`

Iterate on an action (performs repetition of the action).

- `max_iteration_count` (int32): Maximum number of iterations to perform.
- `max_duration_us` (int32): Maximum duration in microseconds.
- `max_parallel_iterations` (int64, default=1): The number of iterations to
  perform in parallel (at the same time).
- `open_loop_interval_ns` (int64): Interval, in nano-seconds, for open loop
  iterations.
- `open_loop_interval_distribution` (string, default=constant):
  - `sync_burst`: all the instances will _try_ to perform the action at the same
    time.
  - `constant`: run at a constant interval.

### message `RpcSpec`

- `name` (string): name the RPC.
- `client` (string): The client `service` (initiator of the RPC)
- `server` (string): The server `service` (target of the RPC)
- `request_payload_name` (string): PayloadSpec to use as a payload for the
  request
- `response_payload_name` (string): PayloadSpec to use as a payload for the
  response
- `fanout_filter` (string, default=all): select the instance(s) of `server` to
  send the RPC to.
  - `all`: Send the RPC to all the instances of `server`, every time.
  - `random`: Choose a random instance.
  - `round_robin`: Choose one instance in a round-robin fashion.
  - `stochastic`: Allow to specify a list of probability to reach a different
    number of instances.
    - Format: `stochastic{probability:nb_targets,...}`
    - Example: `stochastic{0.7:1,0.2:3,0.1:5}` will targets:
      - A single instance of `server` with 70% chance
      - 3 random instances of `server` with 20% chance
      - 5 random instances of `server` with 10% chance
  - An unrecognized value will target the instance 0 of `server`.
- `tracing_interval` (int32)
  - **0**: Disable tracing
  - **>0**: Create a trace of the RPC in the report every `tracing_interval`
    times (`rpc.id % tracing_interval == 0`).

### message `PayloadSpec`

Define the payload attached to an RPC.

- `name` (string): name of the PayloadSpec.
- `size` (int32): The size, in bytes, of the payload

### message `ProtocolDriverOptions`

Configure the protocol driver options. It can be refered by the service message.

- `name` (string): name the ProtocolDriverOptions
- `protocol_name` (string): name of the protocol driver to use (e.g. `grpc`,
  `grpc_async_callback`)
- `netdev_name` (string): name of the network device interface to use (e.g.
  `eth0`)
- `server_settings`: Setting to apply to the protocol driver, for example:
  ```
    server_settings {
      name: "grpc.max_send_message_length"
      int_value: 2048
    }
    server_settings {
      name: "grpc.per_message_compression"
      int_value: 1
    }
  ```
  See [GRPC Options](https://grpc.github.io/grpc/core/group__grpc__arg__keys.html)
  for applicable options.

#### grpc Protocol Driver settings

The grpc protocol driver has a `server_type` `server_settings` option to
configure the server:
- `server_type`: `inline` (requests processed inline)  or `handoff`
  (create a thread and use a reactor to respond to incoming RPCs).

The grpc protocol driver also provides a `client_type` `client_settings` option
to configure the client:
- `client_type`: `polling` (uses a completion thread polling the completion
  queue) or `callback` (grpc performs a callback to notify the completion).

The `grpc_async_callback` behaves as a grpc with `client_type=callback` and
`server_type=handoff`; the `grpc_async_callback` is deprecated, use the grpc
protocol driver with the correct `client_type` and `server_type` options.

## message `DistributionConfig`

This describes a (possibly) multi-dimensional joint distribution. For
 convenience it is possible to describe a one dimension distribution as a CDF.
 For the more general multi-dimensional case, each pmf point can describe
 multiple dimensions of the distribution independently, with the meaning of
 each dimension being described by the coresponding `field_names`.

- `name` (string): name the DistributionConfig
- `pmf_points`: This is used to define the probability mass function (PMF)
 of a distribution.
  - The pmf values of all the points in the distribution must add up to
   (or be near) 1.0.
  - The number of data_points must match the number of dimensions of
   the joint distribution.
- `cdf_points`: This can be used to define the cumulative distribution function
 (CDF) of a distribution. 
  - The cdf value of the last cdf_point must be equal to 1.0
  - If the first point's cdf value is equal to zero, then the distribution is
   interpreted as piece-wise uniform with the lower bound of each subsequent
   interval being (the previous value + 1) automatically. E.g.
   values of 5, 10, 25, 40 would define inclusive intervals of [5, 10],
   [11, 25], [26, 40]. Note that N points define N-1 intervals.
- `field_names` (string): The name(s) describing the meaning of each dimension.

Examples of files with distributions in them can be found in the
 [homa_cp_node_configs](../test_builder/homa_cp_node_configs) folder.

### Misc settings

- `default_protocol`: Select the protocol driver to use (by default
  `grpc_async_callback` is used)
  - `grpc`: Use a completion thread to poll the completion queue
  - `grpc_async_callback`: Use the Asynchronous API with a callback function

## Other options

The `TestSequence` RPC also have the following options:

### `tests_setting`

- `keep_instance_log` (boolean, default=true): The test results contains, by
  default, the instance log which include a trace of all the RPCs executed. If
  the output is too verbose, the instance log can be suppressed by assigning
  false to this setting:
  ```yaml
  tests_setting {
    keep_instance_log: false
  }
  tests {
  ...
  ```
- `shutdown_after_tests` (boolean, default=false): If true, quit Distbench (node
  managers & test sequencers) when all the tests in the RPC are done.
