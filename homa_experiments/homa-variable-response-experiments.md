# Homa Experiments with Variable Size Multi level RPC Traffic Pattern

This document describes how to run Distbench tests with Homa using the Variable
 size multi level RPC Traffic Pattern. This traffic pattern is based off the
 the multi level RPC but allows you to specify a range of `response_size`'s
 or an exact size.

First you must set up the experiment by running the
 `deploy_to_cloudlab_with_homa.sh` script. In a dedicated terminal run:

 ```bash
./deploy_to_cloudlab_with_homa.sh user@cloudlabhost num_nodes
 ```

In order for this tool to work, you must have a private key named `cloudlab` in
 `~/.ssh` and a corresponding public key on cloudlab. This allows you to ssh to
 other nodes. To do this:
 - Run `ssh-keygen` in your ssh directory
 - Name the key pair `cloudlab`
 - Upload the public key, `cloudlab.pub` to [cloudlab](https://www.cloudlab.us/)
 by clicking on the menu with your username at the top right corner and selecting
 "manage ssh keys"

NOTE: In case your local username does not match the cloudlab username you can
 set the CLOUDLAB_USER environment variable.

Once the previous script is finished, you can use the `test_builder` on a
 dedicated terminal to generate and execute the desired test sequences. Below is
 an example of a test sequence with 10 nodes running homa at 10,000 qps with a
 response size range between 1024 bytes and 64,000 bytes.

```bash
test_builder -s localhost:11000 -o my_data_dir variable_multi_level_pc:homa:root_count=2:leaf_count=7:lower_size=1024:upper_size=64000:qps=10000
```
