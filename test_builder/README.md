# Test builder

Test builder is a comprehensive tool used to easily generate test sequences
based on parameterized workloads and protocol drivers.

It can currently generate test sequences for the following workloads (see
[Distbench Test Workloads](../workloads/README.md) for more information).
- Client Server
- Clique
- Tripartite
- Multi Level RPC
(Those workloads are defined at the end of [test\_builder source](test_builder)
by functions such as `client_server_traffic_pattern_setup`).

And for the following protocol drivers:
- grpc

It can be easily extended to support more protocol drivers and workloads.

Use the help option to get a comprehensive introduction to the `test_builder`
tool:
```bash
$ ./test_builder --help
```

