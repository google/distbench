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
# This tool installs the homaModule on all the nodes in the specified cluster.
#
# NOTE: In order for this tool to work, you must have a key named 'cloudlab' in
# ~/.ssh and a corresponding public key uploaded onto cloudlab. This allows you
# to ssh to other nodes from node0. To do this:
#   1. Run the following command:
#       ssh-keygen -f ~/.ssh/cloudlab -N ""
#   2. Upload the public key, '~/.ssh/cloudlab.pub' to cloudlab at
#      https://www.cloudlab.us/ssh-keys.php
#   3. Restart any experiments to enable the new key.
set -eu

function clssh() { ssh -o 'StrictHostKeyChecking no' -o "User ${CLOUDLAB_USER:-$USER}" "${@}"; }
function clscp() { scp -o 'StrictHostKeyChecking no' -o "User ${CLOUDLAB_USER:-$USER}" "${@}"; }

if [[ $# = 0 || $# -gt 2 ]]; then
  echo "usage:$0 cloudlabhost [num_nodes]" > /dev/stderr
  echo "Set the CLOUDLAB_USER environment variable to use a different remote username"
  exit 1
fi

if [[ ! -f "${HOME}/.ssh/cloudlab" ]]; then
  echo "No cloudlab-specific key found. please run the following command:"
  echo 'ssh-keygen -f ~/.ssh/cloudlab -N ""'
  echo  "and then upload ${HOME}/.ssh/cloudlab.pub to the cloudlab ssh key page at"
  echo "https://www.cloudlab.us/ssh-keys.php"
  echo "***You will need to restart your experiment before this key will work**"
  exit 1;
fi

CLOUDLAB_HOST="$1"
NUM_NODES="${2-$(clssh $CLOUDLAB_HOST cat /etc/hosts | grep ^10 | wc -l)}"

# Copy the cloudlab-specific private key:
echo "Copying cloudlab-specific ssh key to ${CLOUDLAB_HOST}..."
clscp ${HOME}/.ssh/cloudlab "$CLOUDLAB_HOST":~/.ssh/id_rsa
sleep 1

# Invoke the HomaModule install script:
clssh "$CLOUDLAB_HOST" bash  << EOF
  #This runs on the remote system:
  echo -e "\nPreparing to install homa:"
  echo -n "Running as user: "
    whoami
  echo -n "Running on host: "
    hostname
  set -ex
  rm -rf homaModule
  git clone https://github.com/PlatformLab/HomaModule.git homaModule
  cd homaModule
  make all -j
  ! sudo rmmod homa
  sudo insmod homa.ko
  make -C util -j
  rm -rf ~/bin
  mkdir -p ~/bin
  PATH=$PATH:~/bin
  cp cloudlab/bashrc ~/.bashrc
  cp cloudlab/bash_profile ~/.bash_profile
  cp cloudlab/gdbinit ~/.gdbinit
  cp cloudlab/bin/config ~/bin
  cloudlab/bin/install ${NUM_NODES}
EOF
