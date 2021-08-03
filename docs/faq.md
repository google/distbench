# Distbench Frequently Asked Questions (FAQ)

## Proxy issue

Distbench will attempt to honour the proxy environment variables (`grpc_proxy`,
`https_proxy`, `http_proxy`, `no_grpc_proxy`, `no_proxy`). In most cases, you
probably want to run the benchmark without using a proxy. For this, unset the
proxy variables to run:

```bash
http_proxy="" https_proxy="" command
```

## Running on WSL (Windows Subsystem for Linux)

Running on WSL, you will probably run into the following issue:

```
socket_utils_common_posix.cc:224] check for SO_REUSEPORT: {...}
```

There is currently no known workaround.

