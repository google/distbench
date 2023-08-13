# cp_node_vs_distbench experiments

The cp_node_vs_distbench.sh script allows you to run the cp_node experiments on cloudlab using both cp_vs_tcp script and distbench.

You have run the distbench deploy_to_cloudlab.sh script in another terminal and homa installed in the cluster. You also need to build distbench and the analysis directory.

## Environment variables:
-   **CLOUDLAB_USER** (optional) remote user name to use for ssh/scp.

## Flags
-    **-h** host to run homa experiment scripts from
-    **-x** experiments you want to run in distbench (-x homa -x grpc -x grpc:server_type=$SERVER_TYPE -x grpc:server_type=SERVER_TYPE:transport=homa etc. Default: homa and grpc)
-    **-n** number of nodes (default = determined from /etc/hosts on the given host)
-    **-w** workload (default = w5)
-    **-b** Gbps for the test (default = 1)
-    **-s** seconds to run the tests (default = 10)
-    **-o** output directory where the results of each experiment are gonna be stored in .rtt file extension as well as the configs and binary outputs for the distbench experiments.

## Usage:
```bash
    ./cp_node_vs_distbench.sh [HOSTNAME] [options]
```
