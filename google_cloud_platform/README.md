# Distbench on Google Cloud Platform

## Introduction

## Requisites and Authentification

### Ansible

Install Ansible and the requisites to use the GCP modules.

See the Ansible documentation at:
<https://docs.ansible.com/ansible/latest/scenario_guides/guide_gce.html#introduction>

On Ubuntu:
```bash
$ sudo apt update
$ sudo apt install ansible
$ apt install python3-pip
$ pip install requests google-auth
```

### Add the necessary credentials

1. Create a Service Account
2. Download the JSON credentials

### Update the configuration

Edit the configuration in `group_vars/all.yml`;
set `gcp_project` and `gcp_service_account_file` to match your GCP
creditentials.

Other options can be customized as well, especially:
- `gcp_machine_type`: Type of the instance to start
- `gcp_number_of_node_managers`: Number of node managers to start

### Install Blaze

If not already installed, Blaze needs to be installed; it will
be used to build distbench on the local machine. The binary
will be copied to the cluster.

On Ubuntu, you can use:
```bash
$ ansible-playbook playbook_install_blaze_localhost.yml
```

For other distributions, follows the instructions at:
<https://docs.bazel.build/versions/master/install.html>.

## Deploy a GCP cluster

Once the configuration is done, you can deploy with:
```bash
./run_playbook.sh
```

The IP of the test-sequencer instance will be shown in the output as:
```

PLAY [Distbench test_sequencer] ***********************

TASK [Gathering Facts] ********************************
ok: [10.240.0.12]
```

Once fully deployed; a test can be run as:
```
$ cd ../workloads
$ ./simple_client_server_rpc_pattern.sh -s 10.240.0.12:10000
(...)
  log_summary: "RPC latency summary:\nclient_server_rpc: N: 100 min: 789198ns median: 1705623ns 90%: 2667703ns 99%: 4064675ns 99.9%: 4064675ns max: 4064675ns\n"
}
Rpc succeeded with OK status
```


## Clean-up

To remove the instances (and being charged), run the clean-up scripts. It will
remove the instances, the public ips and the created disks.

Make sure that the number of node managers hasn't been changed in the
configuration, so that all the nodes are removed.

```bash
ansible-playbook clean-up.yml
```

## Known-issues and workarounds

### Restrictive network preventing outgoing ssh

You can create a control instance attached to the
  ansible-distbench-network-instance network. From which you can run the
  `./run_playbook.sh` script.

For the virtual machines to be reachable, change the playbook.yml `post_tasks`
to register the private IPs to the inventory instead of the public ones:
- For private: `{{ instances.results[ansible_loop.index0].networkInterfaces[0].networkIP }}`
- For public: `{{ addresses.results[ansible_loop.index0].address }}`


