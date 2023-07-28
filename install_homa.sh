#!/bin/bash
#This script downloads the homaModule on all the nodes in the specified cluster
#Assumes your key for cloudlab is named cloudlab
if [[ $# -ne 2 ]]; then
  echo usage:$0 user@cloudlabhost num_nodes > /dev/stderr
  exit 1
fi
scp ~/.ssh/cloudlab "$1":~/.ssh/id_rsa
NUM_NODES=$2
ssh -o 'StrictHostKeyChecking no' \
  "$1" \
  bash  << EOF
#This runs on the remote system:
whoami
hostname
set -ex
rm -rf homaModule
rm -rf HomaModule
git clone https://github.com/PlatformLab/HomaModule.git homaModule
cd homaModule
make all
! sudo rmmod homa
sudo insmod homa.ko
cd util
make
cd ..
rm -rf ~/bin
mkdir -p ~/bin
PATH=$PATH:~/bin
(cd cloudlab; \
cp bashrc ~/.bashrc; \
cp bash_profile ~/.bash_profile; \
cp gdbinit ~/.gdbinit)
cd cloudlab/bin
cp config ~/bin
./install ${NUM_NODES}
EOF
