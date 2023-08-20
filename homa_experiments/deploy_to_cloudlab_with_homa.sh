#!/bin/bash
################################################################################
# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
# This is a tool for installing the homa module on all the nodes in a cloudlab
# cluster, and setting up an experiment on cloudlab using deploy_to_cloudlab.sh
#
# Arguments:
#   1. The hostname of a machine in your cloudlab experiment cluster
#      i.e. node0.distbench.uic-dcs-pg0.utah.cloudlab.us
#   2. (optional) The number of nodes to use in the cluster
#
# NOTE: In order for this tool to work, you must have a key named 'cloudlab' in
# ~/.ssh and a corresponding public key uploaded onto cloudlab. This allows you
# to ssh to other nodes from node0. To do this:
#   1. Run 'ssh-keygen -f ~/.ssh/cloudlab -N ""'
#   2. Upload the public key, '~/.ssh/cloudlab.pub' to cloudlab at
#      https://www.cloudlab.us/ssh-keys.php
#   3. Restart any cloudlab experiments that you may have started.
#
# NOTE: In case your local username does not match the cloudlab username
# you can set the CLOUDLAB_USER environment variable.
set -eu
DISTBENCH_SRC=$(readlink -f $(dirname $(which $0))/..)
source "${DISTBENCH_SRC}/common_status.sh"

if [[ $# = 0 || $# -gt 2 ]]; then
  echo usage:$0 cloudlabhost > /dev/stderr
  echo "The CLOUDLAB_USER env var can be used to set remote user name"
  exit 1
fi

function clssh() { ssh -o 'StrictHostKeyChecking no' -o "User ${CLOUDLAB_USER:-$USER}" "${@}"; }

CLOUDLAB_HOST="$1"
clssh $CLOUDLAB_HOST whoami > /dev/null || (
  if [[ ! -v CLOUDLAB_USER ]]; then
    echo_error red "Your ssh connection does not seem to work. If your are"
    echo "certain that your cluster is up and running, then you may need to set the"
    echo_error red "CLOUDLAB_USER environment variable if your remote username is not"
    echo_error red "the same as your local username."
  fi
  exit 1
)

echo_magenta "Getting cluster domain name..."
CLUSTER_DOMAINNAME="$(clssh $CLOUDLAB_HOST hostname | cut -f 2- -d.)"

echo_magenta "Getting cluster size..."
NUM_NODES="${2-$(clssh $CLOUDLAB_HOST cat /etc/hosts | grep ^10 | wc -l)}"

echo_green -e "\nYour cluster seems to be named ${CLUSTER_DOMAINNAME} and consist of ${NUM_NODES} nodes\n"

echo_magenta "Installing homa module on the cluster..."
"${DISTBENCH_SRC}/homa_experiments/install_homa.sh" ${CLOUDLAB_HOST} ${NUM_NODES}

GIT_REPO=localnocheck
GIT_BRANCH=$(git -C "${DISTBENCH_SRC}" branch --show-current)
echo_magenta -e "\n\nDeploying distbench from repo ${GIT_REPO} branch ${GIT_BRANCH} to the cluster..."
"${DISTBENCH_SRC}/deploy_to_cloudlab.sh" "${CLUSTER_DOMAINNAME}" "${GIT_REPO}" "${GIT_BRANCH}" "${NUM_NODES}"
