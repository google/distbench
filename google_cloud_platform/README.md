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

The other options can be customized as well.
