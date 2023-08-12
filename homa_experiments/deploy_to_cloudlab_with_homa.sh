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
#   1. Run 'ssh-keygen -f ~/.ssh/cloudlab -N ""'
#   2. Upload the public key, 'cloudlab.pub' to cloudlab at
#      https://www.cloudlab.us/ by clicking the 'manage SSH keys'
#      option on the menu that appears when you click your username
#   3. Restart any cloudlab experiments that you may have started.
#
# NOTE: In case your local username does not match the cloudlab username
# you can set the CLOUDLAB_USER environment variable.
set -eu
PATH+=":$PWD"

if [[ $# -ne 1 ]]; then
  echo usage:$0 user@cloudlabhost > /dev/stderr
  exit 1
fi

function clssh() { ssh -o 'StrictHostKeyChecking no' -o "User ${CLOUDLAB_USER:-$USER}" "${@}"; }

USER_HOST="$1"
echo "Getting cluster domain name"
CLUSTER_DOMAINNAME="$(clssh $USER_HOST hostname | cut -f 2- -d.)"
echo "Getting cluster size"
NUM_NODES="$(clssh $USER_HOST cat /etc/hosts | grep node | grep ^10 | wc -l)"
echo -e "\n\nYour cluster seems to be named ${CLUSTER_DOMAINNAME} and consist of ${NUM_NODES} nodes\n"
GIT_REPO=https://github.com/google/distbench.git
GIT_BRANCH=main

echo "Installing homa module on cluster"
./install_homa.sh ${USER_HOST} ${NUM_NODES}

cd ..
echo "Deploying to cloudlab"
./deploy_to_cloudlab.sh ${CLUSTER_DOMAINNAME} ${GIT_REPO} ${GIT_BRANCH} ${NUM_NODES}
