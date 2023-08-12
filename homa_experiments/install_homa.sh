#!/bin/bash
# This tool downloads the homaModule on all the nodes in the specified cluster
#
# NOTE: In order for this tool to work, you must have a key named 'cloudlab' in
# ~/.ssh and a corresponding public key uploaded onto cloudlab. This allows you
# to ssh to other nodes from node0. To do this:
#   1. Run 'ssh-keygen' in your ssh directory
#   2. Name the key pair 'cloudlab'
#   3. Upload the public key, 'cloudlab.pub' to cloudlab at 
#      https://www.cloudlab.us/ by clicking the 'manage SSH keys'
#      option on the menu that appears when you click your username
set -eu

if [[ $# -ne 2 ]]; then
  echo usage:$0 user@cloudlabhost num_nodes > /dev/stderr
  exit 1
fi
if [[ ! -f "${HOME}/.ssh/cloudlab" ]]; then
  #echo "Creating a cloudlab-specific ssh key file in ${HOME}/.ssh/cloudlab"
  #ssh-keygen -f ~/.ssh/cloudlab -N ""
  echo "No cloudlab-specific key found. please run the following command:"
  echo 'ssh-keygen -f ~/.ssh/cloudlab -N ""'
  echo  "and then upload ${HOME}/.ssh/cloudlab.pub to the cloudlab ssh key page at"
  echo "https://www.cloudlab.us/ssh-keys.php"
  echo "***You will need to restart your experiment before this key will work**"
  exit 1;
fi

scp ${HOME}/.ssh/cloudlab "$1":~/.ssh/id_rsa
NUM_NODES=$2
ssh -o 'StrictHostKeyChecking no' \
  "$1" \
  bash  << EOF
#This runs on the remote system:
whoami
hostname
set -ex
rm -rf homaModule
git clone https://github.com/PlatformLab/HomaModule.git homaModule
cd homaModule
make all
! sudo rmmod homa
sudo insmod homa.ko
make -C util
rm -rf ~/bin
mkdir -p ~/bin
PATH=$PATH:~/bin
cp cloudlab/bashrc ~/.bashrc
cp cloudlab/bash_profile ~/.bash_profile
cp cloudlab/gdbinit ~/.gdbinit
cp cloudlab/bin/config ~/bin
cloudlab/bin/install ${NUM_NODES}
EOF
