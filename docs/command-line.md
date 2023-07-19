# Distbench command line arguments documentation

Distbench is organized in different modules (similar to Busybox).

This document describes the different modules and their associated parameters.

## `test_sequencer`

Start the test sequencer. Node managers will register themselves to the test
sequencer. Once the node managers registeres you can submit test sequences to
be executed.

``` bash
distbench test_sequencer [--port=port_number]
```

Options:
- `--port=port_number`: Specify the port number for the test\_sequencer to
  listen on. The port is used both for the `node_manager` to connect and for
  the client to send `TestSequence` to execute.

## `node_manager`

Start a node manager and connect to the specified `test_sequencer`. The
node manager will effectively execute the submitted tests.

``` bash
distbench node_manager [--port=port_number] [--test_sequencer=host:port]
```

Options:
- `--test_sequencer=h:p`: The host:port of the `test_sequencer` to connect to.
- `--port=port_number`: The port for the `node_manager` to listen on.

## `run_tests`

Connect to a `test_sequencer` and send the specified TestSequence protobuf. Once
the execution is completed, a summary will be displayed. If a result filename is
specified, the TestResult will also be saved.

``` bash
distbench run_tests [--infile test_sequence.proto_text]
                    [--outfile result.proto]
                    [--test_sequencer=host:port]
                    [--binary_output]
                    [--max_test_duration=duration]
                    --test_sequencer=host:port
```

Options:
- `--test_sequencer=h:p`: The host:port of the `test_sequencer` to connect to.
- `--binary_output`: Save the test result protobuf in binary
- `--infile test_sequence.proto_text`: The protobuf filename of the test
  sequence to submit to the test sequencer (default to /dev/stdin).
- `--outfile result.proto`: The protobuf filename that is used to save the test
  result.  Specify `--binary_output` to save in binary mode. An empty filename
  (--outfile "") will suppress the output (only the test summary will be
  displayed).
- `--max_test_duration=duration`: Set the maximum time for each test
  specified in the test sequence proto. If unspecified by this flag or in the
  test sequence proto, it will default to 1 hour.

## `check_test`

This module is useful to test whether your textproto config file is valid. It
will attempt to parse the file, then print the result.

``` bash
distbench check_test [--infile test_sequence.proto_text]
```

Options:
- `--infile test_sequence.proto_text`: The protobuf filename of the test
  sequence to submit to the 'check_test' module.

## `test_preview`

This module is useful if you would like to start developing an analysis tool
 for your test(s). It will run a specified TestSequence on local host with a
 single node manager. If a result filename is specified, the TestResult will be
 saved (default stdout).
``` bash
distbench test_preview [--infile test_sequence.proto_text]
                       [--outfile result.proto]
```

Options:
- `--binary_output`: Save the test result protobuf in binary
- `--infile test_sequence.proto_text`: The protobuf filename of the test
  sequence to submit to the 'test_preview' module.
- `--outfile result.proto`: The protobuf filename that is used to save the test
  result. Specify `--binary_output` to save in binary mode.

## help

Display a simple summary of the available commands.

## Other options

- `--prefer_ipv4`: By default, Distbench will use IPV6 addresses if
  available, use the `--prefer_ipv4` flags to use IPV4 addresses instead.

- `--default_data_plane_device`: Specify an interface for Distbench to use for
  the data plane (such as eth0). By default or if an empty string is specified,
  Distbench will attempt to guess the most suitable interface. This setting can
  be overridden by the test sequence.

- `--control_plane_device`: Specify an interface for Distbench to use for
  control RPCs. This may be necessary if the default primary interface is on a
  slow or congested network.
