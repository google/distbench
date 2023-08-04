# cp_node_vs_distbench experiments

The cp_node_vs_distbench.sh script allows you to run the cp_node experiments on cloudlab using both cp_vs_tcp script and distbench.

You have run the distbench deploy_to_cloudlab.sh script in another terminal and homa installed in the cluster. You also need to build distbench and the analysis directory.

The script takes the DNS (USER@HOSTNAME) as first optional argument and the flags to specify the parameters of the experiment are:

## Flags
-    **-x** experiments you wanna run in distbench (-x homa -x grpc -x grpc:server_type=$SERVER_TYPE -x grpc:server_type=SERVER_TYPE:transport=homa etc. Default: homa and grpc)
-    **-n** number of nodes (default = 10)
-    **-w** workload (default = w5)
-    **-b** Gbps for the test (default = 1)
-    **-s** seconds to run the tests (default = 10)
-    **-o** output directory where the results of each experiment are gonna be stored in .rtt file extension as well as the configs and binary outputs for the distbench experiments.

## Usage:
```bash
    ./cp_node_vs_distbench.sh [USER@HOSTNAME] [options]
```