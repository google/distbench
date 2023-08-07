#!/bin/bash
# This is a tool for installing the homa module on all the nodes in a cloudlab
# cluster, and setting up an experiment on cloudlab using deploy_to_cloudlab.sh
#
# Arguments:
#   1. user@cloudlabhost
#   2. Number of nodes in the cluster
#
# NOTE: In order for this tool to work, you must have a key named 'cloudlab' in
# ~/.ssh and a corresponding public key uploaded onto cloudlab. This allows you
# to ssh to other nodes from node0. To do this:
#   1. Run 'ssh-keygen' in your ssh directory
#   2. Name the key pair 'cloudlab'
#   3. Upload the public key, 'cloudlab.pub' to cloudlab at
#      https://www.cloudlab.us/ by clicking the 'manage SSH keys'
#      option on the menu that appears when you click your username
#
# NOTE: In case your local username does not match the cloudlab username
# you can set the CLOUDLAB_USER environment variable.
set -ex
PATH+=":$PWD"

if [[ $# -ne 2 ]]; then
  echo usage:$0 user@cloudlabhost num_nodes > /dev/stderr
  exit 1
fi

USER_HOST="$1"
NUM_NODES=$2
echo "Installing homa module on cluster"
./install_homa.sh ${USER_HOST} ${NUM_NODES}

echo "Getting cluster domain name"
CLUSTER_DOMAINNAME="$(ssh $USER_HOST hostname | cut -f 2- -d.)"
GIT_REPO=https://github.com/google/distbench.git
GIT_BRANCH=main

cd ..
echo "Deploying to cloudlab"
./deploy_to_cloudlab.sh ${CLUSTER_DOMAINNAME} ${GIT_REPO} ${GIT_BRANCH} ${NUM_NODES}