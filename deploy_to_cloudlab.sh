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
# deploy_to_cloudlab.sh:
# A script to automate setting up distbench experiments in cloudlab.
#
# To start, visit the following URL to startup a cluster in cloudlab:
# https://tinyurl.com/36ypzsvh
# (hint: add this to your bookmarks)
#
# You can use a different experiment profile, but the machines must be named
# nodeN with N starting at 0, and should be of a single hardware model.
#
# After the cluster is up and running, execute this script to download,
# build, deploy, and run distbench on the experiment cluster. The logs for
# the most recent run of all the instances of distbench will be collected
# on node0.
#
# The distbench node managers and test sequencer will be bound to the
# cluster's private IP address, making them unreachable over the public
# internet. However the test sequencer RPC interface will be reachable
# from the machine that this script is running on via ssh port forwarding
# as localhost:11000. E.g.
# test_builder client_server -s localhost:11000 -o my_data_dir
#
# If this script is run multiple times it will kill previous instances
# and build/deploy an up-to-date distbench binary from the selected
# branch of the select repository. Do not attempt to manually work with
# the git repositories in the cloudlab hosts' distbench_repo/ or (if applicable)
# distbench_mirror_of_local/ directories, as they will likely be overwritten
# by a subsequent run of this script, making you very sad.
# Please submit your changes to github or a local branch instead.
#
# This script takes 5 optional arguments
# 1) The DNS domain name (not hostname) of the experiment cluster.
#    If you are not sure, try the real hostname of one of the nodes, or
#    run "hostname" on one of the nodes.
# 2) The git repository URL to fetch from, or "local" to push the local
#    git repository to the cloudlab hosts. When specifying local the
#    repository will be checked to make sure that there are no uncommited
#    changes. This check can be skipped by specifying localnocheck.
# 3) The name of the git branch to use when building distbench.
# 4) The number of nodes to use within the cluster.
#    If this argument is omitted DNS is queried to find out
#    the number of nodes to use.
# 5) The name of the netdev to use for distbench traffic.
#    If this argument is omitted it is automatically determinied.
#
# For convenience feel free to edit the following 3 lines.

DEFAULT_CLUSTER_DOMAINNAME=distbench.uic-dcs-pg0.utah.cloudlab.us
DEFAULT_GIT_REPO=https://github.com/google/distbench.git
DEFAULT_GIT_BRANCH=main

################################################################################
# GIANT WARNING! GIANT WARNING! GIANT WARNING! GIANT WARNING! GIANT WARNING!
# DO NOT MAKE CHANGES BELOW THIS LINE, UNLESS YOU PLAN TO UPSTREAM THEM.
################################################################################
set -uEeo pipefail
shopt -s inherit_errexit

function unknown_error_shutdown() {
  echo_error red "\\nError, unknown_error_shutdown invoked status = $?"
  echo_error red "\\n  Failed command:\n  $BASH_COMMAND"
  jobs
  exit
}

trap unknown_error_shutdown ERR

ECHODELAY=0.005 source common_status.sh

function clssh() { ssh -o 'StrictHostKeyChecking no' "${@}"; }

sh_files=(common_status.sh git_clone_or_update.sh)

ALL_FILES_PRESENT=true
for file in "${sh_files[@]}"
do
  if [[ ! -f "$file" ]]
  then
    echo "Missing required file: $file"
    ALL_FILES_PRESENT=false
  fi
done

if [[ "$ALL_FILES_PRESENT" != "true" ]]
then
  echo "This script is expected to run from a complete git repository."
  exit 1
fi

if [[ $# -gt 5 ]]
then
  echo_error red "$0 takes at most 5 arguments"
  exit 1
fi

CLUSTER_DOMAINNAME=${1:-${DEFAULT_CLUSTER_DOMAINNAME}}
GIT_REPO=${2:-${DEFAULT_GIT_REPO}}
GIT_BRANCH=${3:-${DEFAULT_GIT_BRANCH}}
declare -i NUM_NODES=${4:-0}
PRIVATE_NETDEV=${5:-}

if [[ "${GIT_REPO}" == "localnocheck" ]]
then
  GIT_REPO=local
elif [[ "${GIT_REPO}" == "local" ]]
then
  echo_green "Checking that the local git tree is checked-in..."
  git diff --stat --exit-code || (
    echo_error red "  You need to run git commit before $0."
    echo_error red "  If you are just hacking you can do"
    echo_blue "  git commit -a -m hacking"
    exit 1
  )
  echo
fi

if ping -c 1 "${CLUSTER_DOMAINNAME}" &> /dev/null
then
  echo_green "It looks like ${CLUSTER_DOMAINNAME} is a hostname."
  echo_green "  Attempting to convert it to a domainname..."
  CLUSTER_DOMAINNAME="$(clssh ${CLUSTER_DOMAINNAME} hostname |cut -f 2- -d.)"
  echo_green "    Using cluster name ${CLUSTER_DOMAINNAME}\n"
fi

echo_green "Setting up experiment cluster ${CLUSTER_DOMAINNAME} ..."
echo_green "  Using git repo: ${GIT_REPO} branch: ${GIT_BRANCH}"

if [[ ${NUM_NODES} -le 0 ]]
then
  echo_green "\\nCounting nodes in experiment cluster..."
  NUM_NODES=0
  while nslookup node${NUM_NODES}.${CLUSTER_DOMAINNAME} >/dev/null 2>&1
  do
    NUM_NODES+=1
  done
  echo_green "  Counted $NUM_NODES nodes in experiment cluster."
  if [[ "${NUM_NODES}" == "0" ]]
  then
    echo_error red "  Experiment cluster may not be ready yet, or nonexistent."
    exit 1
  fi
else
  if ! ping -c 1 node$((NUM_NODES-1)).${CLUSTER_DOMAINNAME} > /dev/null
  then
    echo_error red "  Experiment cluster may not be ready yet, or undersized."
    exit 1
  fi
  echo_green "\\nUsing $NUM_NODES nodes in experiment cluster."
fi

NODE0=node0.${CLUSTER_DOMAINNAME}

if [[ "${GIT_REPO}" == "local" ]]
then
  UPSTREAM_URL="https://github.com/google/distbench.git"
  MIRROR_GIT=/users/${USER}/distbench_mirror_of_local/
  echo_green "\\nPushing local git repo to cluster..."
  if ! clssh ${NODE0} test -d "${MIRROR_GIT}"
  then
    echo_green "  Cloning from upstream first to save bandwidth..."
    clssh ${NODE0} \
      "rm -rf distbench_mirror_of_local &&
       git clone --bare ${UPSTREAM_URL} distbench_mirror_of_local"
    echo_green "  Incrementally updating with local commits..."
  fi
  ! git remote remove mirror_of_local &> /dev/null
  git remote add mirror_of_local \
    ssh://${USER}@${NODE0}/users/${USER}/distbench_mirror_of_local
  git push -f mirror_of_local --all
  git remote remove mirror_of_local
  GIT_REPO="${MIRROR_GIT}"
fi

# TODO(danmanj) rewrite all this using the jq tool:
if [[ -n "${PRIVATE_NETDEV}" ]]
then
  echo_green "\\nUsing netdev ${PRIVATE_NETDEV} ..."
else
  echo_green "\\nPicking private netdev to use..."
  PUBLIC_HOSTNAME=$(clssh ${NODE0} hostname -f)
  PUBLIC_IP=$(host ${PUBLIC_HOSTNAME} | cut -f 4 -d" ")
  netdev_list=($(clssh ${NODE0} ip -br link list |
                   grep LOWER_UP |
                   grep -v lo |
                   cut -f1 -d " "))
  if [[ ${#netdev_list[@]} -eq 0 ]]
  then
    echo_error red "\\nNo netdevs returned"
    exit 1
  fi
  for netdev in "${netdev_list[@]}"
  do
    echo_green "  Trying netdev $netdev..."
    if clssh ${NODE0} ip address show dev $netdev | grep $PUBLIC_IP &> /dev/null
    then
      echo_green "    Netdev ${netdev} is the public interface."
      PUBLIC_NETDEV=${netdev}
    else
      echo_green "    Netdev ${netdev} is a private interface."
      PRIVATE_NETDEV=${netdev}
      break
    fi
  done
fi

CONTROL_NETDEV=${PRIVATE_NETDEV}
TRAFFIC_NETDEV=${PRIVATE_NETDEV}

CONTROL_IP4=$(
  addr_line=$(clssh ${NODE0} ip -br -4 address show dev ${CONTROL_NETDEV})
  if [[ -z "${addr_line}" ]]
  then
    echo_error yellow "  No IPv4 address associated with ${CONTROL_NETDEV}"
  else
    echo "${addr_line}" | (IFS=" /" ;read a b c d; echo $c)
  fi
)

CONTROL_IP6=$(
  addr_line=$(clssh ${NODE0} ip -br -6 address show dev ${CONTROL_NETDEV})
  if [[ -z "${addr_line}" ]]
  then
    echo_error yellow "  No IPv6 address associated with ${CONTROL_NETDEV}"
  else
    echo "${addr_line}" | (IFS=" /" ;read a b c d; echo $c)
  fi
)

if [[ -z "$CONTROL_IP4" && -z "$CONTROL_IP6" ]]
then
  echo_error red "  No IP address associated with ${CONTROL_NETDEV}"
  exit 1
fi

if [[ "${CONTROL_IP6:0:4}" == "fe80" || -z "$CONTROL_IP6" ]]
then
  SEQUENCER_IP=${CONTROL_IP4}
else
  SEQUENCER_IP=${CONTROL_IP6}
fi

echo_green "\\nUsing ${SEQUENCER_IP} for sequencer IP"

SEQUENCER_PORT=10000

function launch_remote() {
  echo_green "\\nExecuting bootstrap script on main node..."

  # For debugability change the clsh command to be
  # "export TERM=$TERM; tee debug.sh | bash /dev/stdin"
  # (include the quotes)
  # The double -t sends SIGHUP to the remote processes when the local ssh client
  # is killed by e.g. SIGTERM.
  clssh -t -t -L 11000:${SEQUENCER_IP}:${SEQUENCER_PORT} ${NODE0} \
    "export TERM=$TERM; stty -echo ; bash /dev/stdin" \
    "${NUM_NODES}" \
    "${GIT_REPO}" \
    "${GIT_BRANCH}" \
    "${SEQUENCER_IP}" \
    "${SEQUENCER_PORT}" \
    "${CONTROL_NETDEV}" \
    "${TRAFFIC_NETDEV}"
}

(cat ${sh_files[@]} /dev/stdin | launch_remote) << 'EOF'
######################## REMOTE SCRIPT BEGINS HERE #############################
# We must enclose the contents in () to force bash to read the entire script
# before execution starts. Otherwise commands reading from stdin may steal the
# text of the script.
(
set -uEeo pipefail
shopt -s inherit_errexit

if [[ $# != 7 ]]
then
  echo_error red "Remote script needs exactly 7 arguments to proceed"
  exit 1
fi

declare -i NUM_NODES="${1}"
GIT_REPO="${2}"
GIT_BRANCH="${3}"
SEQUENCER_IP="${4}"
SEQUENCER_PORT="${5}"
CONTROL_NETDEV="${6}"
TRAFFIC_NETDEV="${7}"

function cloudlab_ssh() { sudo ssh -o 'StrictHostKeyChecking no' "${@}"; }

function cloudlab_scp() { sudo scp -o 'StrictHostKeyChecking no' "${@}"; }

function unknown_error_shutdown() {
  echo_error red "\\nError, unknown_error_shutdown invoked status = $?"
  echo_error red "\\n  Failed command:\n  $BASH_COMMAND"
  jobs
  exit
}

trap unknown_error_shutdown ERR

echo_magenta "\\nRemote bootstrap script executing..."
SEQUENCER=${SEQUENCER_IP}:${SEQUENCER_PORT}
HOSTNAME=$(hostname)
CLUSTER_DOMAINNAME=${HOSTNAME#node[0-9].}
NODE0=node0.${CLUSTER_DOMAINNAME}
if [[ "${HOSTNAME}" != "${NODE0}" ]]
then
  echo_error red "Hostname '${HOSTNAME}' does not follow expected format." \
                 "\\nshould be ${NODE0}"
  exit 1
fi

GITDIR="${PWD}/distbench_repo"
WORKTREE="${GITDIR}/${GIT_BRANCH}"
git_clone_or_update \
  ${GIT_REPO} \
  ${GIT_BRANCH} \
  ${GITDIR} \
  ${WORKTREE}

echo_magenta "\\nChecking for working copy of bazel..."
pushd ${WORKTREE}
bazel-5.4.0 version 2> /dev/null || (
  echo_magenta "  Installing bazel..."
  curl -fsSL https://bazel.build/bazel-release.pub.gpg |
    gpg --dearmor > bazel.gpg
  sudo mv bazel.gpg /etc/apt/trusted.gpg.d/
  dsrc="deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8"
  echo "$dsrc" | sudo tee /etc/apt/sources.list.d/bazel.list
  sudo apt-get update
  sudo apt-get install bazel bazel-5.4.0 -y
)

echo_magenta "\\nChecking for local copy of libfabric/libmercury ..."
LIBFABRIC_VERSION=1.17.0
MERCURY_VERSION=2.2.0
LF_LINK=$(basename $(readlink -smn external_repos/opt/libfabric))
HG_LINK=$(basename $(readlink -smn external_repos/opt/mercury))
if [[ "${LF_LINK:10}" != "${LIBFABRIC_VERSION}" ||
      "${HG_LINK:8}" != "${MERCURY_VERSION}" ]]
then
  sudo apt-get install cmake libhwloc-dev uuid-dev -y &&
  time ./setup_mercury.sh ${LIBFABRIC_VERSION} ${MERCURY_VERSION}
fi

echo_magenta "\\nBuilding distbench binary..."
bazel build -c opt :distbench \
  --//:with-mercury=true \
  --//:with-homa=true \
  --//:with-homa-grpc=true
popd

echo_magenta "\\nKilling any previous distbench processes..."
for i in $(seq 0 $((NUM_NODES-1)))
do
  ping -c 1 node${i}.${CLUSTER_DOMAINNAME} > /dev/null
  ! cloudlab_ssh node${i}.${CLUSTER_DOMAINNAME} \
    "killall -9 distbench_exe ; rm -f ${HOME}/distbench_exe" &
done
wait

echo_magenta "\\nDeploying newest distbench binary as ${HOME}/distbench_exe ..."
cp ${WORKTREE}/bazel-bin/distbench ${HOME}/distbench_exe
for i in $(seq 1 $((NUM_NODES-1)))
do
  cloudlab_scp distbench_exe node${i}.${CLUSTER_DOMAINNAME}:${HOME} &
done
wait

COMMON_ARGS=(
  --prefer_ipv4=true
  --control_plane_device=${CONTROL_NETDEV}
)
TEST_SEQUENCER_ARGS=(
  ${COMMON_ARGS[@]}
  --port=${SEQUENCER_PORT}
)
NODE_MANAGER_ARGS=(
  ${COMMON_ARGS[@]}
  --test_sequencer=${SEQUENCER}
  --default_data_plane_device=${TRAFFIC_NETDEV}
)

echo_blue "\\nStarting Test Sequencer on ${SEQUENCER} ..."
echo_blue "  Debug logs can be found in test_sequencer.log"
GLOG_logtostderr=1 ${HOME}/distbench_exe test_sequencer \
  ${TEST_SEQUENCER_ARGS[@]} \
  2>&1 | tee distbench_test_sequencer.log &
sleep 5

# This is the starting port for node managers. For debuggability this will be
# incremented so that each instance runs on a unique port.
declare -i NODE_MANAGER_PORT=9000

for i in $(seq 0 $((NUM_NODES-1)))
do
  echo_blue "\\nStarting node${i} Node Manager..."
  echo_blue "  Debug logs can be found in node${i}.log"
  # The double -t propgates SIGHUP to all node managers.
  ( ! cloudlab_ssh -t -t node${i}.${CLUSTER_DOMAINNAME} \
    env GLOG_logtostderr=1 sudo -u $USER ${HOME}/distbench_exe node_manager \
        node${i} \
        --port=${NODE_MANAGER_PORT} \
        ${NODE_MANAGER_ARGS[@]} 2>&1 | tee distbench_node_manager${i}.log
    echo_error red "Node manager for node${i} terminated..."
  ) &
  NODE_MANAGER_PORT+=1
  sleep 0.5
done

echo
echo_green "The test sequencer and node managers should now be up and running."
echo_yellow "You should now be able to send tests to localhost:11000 E.g."
echo_cyan "  'test_builder client_server -s localhost:11000 -o my_data_dir'"
echo_yellow "Debug logs can be fetched via"
echo_cyan "  'scp ${NODE0}:distbench*.log my_log_dir'"

wait -n
echo_error red "\\nA distbench process terminated early."
echo_error red "  Look for errors in the log"
) < /dev/null | tee deploy_to_cloudlab.log
EOF
