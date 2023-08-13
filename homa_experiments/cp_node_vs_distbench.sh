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
# A script to compare and visualize distbench vs cp_node performance.
# Requires running distbench deploy_to_cloudlab.sh script in another terminal.
# -x experiments you want to run in distbench
# -n number of nodes (default=10)
# -w workload (default=w5)
# -b Gbps for the test (default=1)
# -s seconds to run the tests (default=10)
# -o output_directory

set -uEeo pipefail
shopt -s inherit_errexit
DISTBENCH_SRC=$(readlink -f $(dirname $(which $0))/..)
ECHODELAY=0.005 source "${DISTBENCH_SRC}/common_status.sh"

NUM_NODES=0
WORKLOAD=w5
GBPS=10
TEST_DURATION=120
OUTPUT_DIRECTORY=
TARGET_HOST=
EXPERIMENTS=()

function print_usage_and_exit {
  echo "To run an experiment you must pass as first argument the USER@HOSTNAME
        from cloudlab and you can add the following flags:
        -x experiments you want to run. Either 'cp_node' or a config for
           distbench, using the test_builder protocol driver
           options syntax (E.g. -x homa:server_threads=2:client_threads=2)
           This can be repeated multiple times to run multiple experiments.
           If left unspecified default values will be used.
        -n number of nodes (default=10)
        -w workload (default=w5)
        -b Gbps for the test (default=1)
        -s seconds to run the tests (default=10)
        -o output_directory (necessary)
        The script also assumes you have deploy_to_cloudlab,sh running in another terminal
        and homa is installed in the cloudlab cluster"
  exit
}

function parse_args () {
  ALLOWED_OPTS="h:x:n:w:b:s:o:"
  parsed_opts="$(getopt -o "${ALLOWED_OPTS}" -- "$@")" || print_usage_and_exit 1
  eval set -- "${parsed_opts}"

  if [[ $# -ne 0 ]]; then
    echo "Unrecognized extra arguments: " "${@}"
    print_usage_and_exit 1
  fi;

  while getopts "${ALLOWED_OPTS}" flag "$@"; do
    case "${flag}" in
      h) TARGET_HOST="${OPTARG}" ;;
      x) EXPERIMENTS+=("${OPTARG}") ;;
      n) NUM_NODES="${OPTARG}" ;;
      w) WORKLOAD="${OPTARG}" ;;
      b) GBPS="${OPTARG}" ;;
      s) TEST_DURATION="${OPTARG}" ;;
      o) OUTPUT_DIRECTORY="${OPTARG}" ;;
      *) print_usage_and_exit;;
    esac
  done

  if [ -z "${OUTPUT_DIRECTORY}" ]; then
    echo "You must specify an output directory (-o)"
    exit
  fi

  if [ -z "${TARGET_HOST}" ]; then
    echo "You must specify a remote host (-h)"
    exit
  fi

  echo "TARGET_HOST is ${TARGET_HOST}"

  if [ ${#EXPERIMENTS[@]} -eq 0 ]
  then
    EXPERIMENTS=(
      cp_node
      grpc
      homa:server_threads=2:client_threads=2
      homa:server_threads=2:client_threads=2:ping_pong=1
    )
    echo "Using default experiments: ${EXPERIMENTS}"
  fi
}

function check_deps() {
  if ! which dist_to_proto &> /dev/null; then
    bazel build "@homa_module//:dist_to_proto"
    PATH+=":${DISTBENCH_SRC}/bazel-bin/external/homa_module"
    ls "${DISTBENCH_SRC}/bazel-bin/external/homa_module"
    if ! which dist_to_proto &> /dev/null; then
      echo "dist_to_proto not found in path, and could not be built"
      exit 1
    fi
  fi

  RESULTS_CONVERSION="${DISTBENCH_SRC}/bazel-bin/analysis/results_conversion"

  if [[ ! -x ${RESULTS_CONVERSION} ]]; then
    echo "$RESULTS_CONVERSION not an executable file, trying to build it"
    (cd ${DISTBENCH_SRC} && bazel  build //analysis:results_conversion) || exit 1
  fi
}

function error_handler() {
  error_code=$?
  echo_error red -e \\n$(caller): Unexpected error $error_code in \\n$BASH_COMMAND
  exit $?
}

trap error_handler ERR

function run_experiments {
  echo "running experiments"
  for exp in ${EXPERIMENTS[@]}
  do
    echo "$exp"
    if [[ "$exp" = "cp_node" ]]; then
      run_cp_node
    else
      TEMPDIR=$(mktemp -d)
      ${DISTBENCH_SRC}/test_builder/test_builder "homa_cp_node:${exp}" \
          -c gbps=${GBPS}:workload=${WORKLOAD}:node_count=${NUM_NODES}:test_duration=${TEST_DURATION} \
          -o ${TEMPDIR} -s localhost:11000

      ${RESULTS_CONVERSION} \
          --input_file=$(ls ${TEMPDIR}/*.pb.gz) \
          --output_directory=${TEMPDIR}/analysis \
          --supress_header \
          --output_format=homa
      mv ${TEMPDIR}/analysis/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_${exp}_${WORKLOAD}.rtts
      rm -rf "${TEMPDIR}/analysis"
      cp "${TEMPDIR}/"* "${OUTPUT_DIRECTORY}"
      rm -rf "${TEMPDIR}"
    fi
  done
}

function clssh() { ssh -o 'StrictHostKeyChecking no' -o "User ${CLOUDLAB_USER:-$USER}" "${@}"; }

function run_cp_node {
  echo "Running cp_node experiments"
  clssh "${TARGET_HOST}"  bash  << EOF
  #This runs on the remote system:
  homaModule/util/cp_vs_tcp -n ${NUM_NODES} -w ${WORKLOAD} --client-ports 1 --server-ports 1 -s ${TEST_DURATION}\
      --tcp-client-ports 1 --tcp-server-ports=1 --port-threads 2 -b ${GBPS} -l ~/logs --port-receivers 2
EOF

  scp ${TARGET_HOST}:~/logs/*.rtts "${OUTPUT_DIRECTORY}"
  cat ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}-*.rtts > ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}.rtts
  cat ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}-*.rtts > ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}.rtts
  rm ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}-*.rtts
  rm ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}-*.rtts
}

parse_args "${@}"
if [[ "$NUM_NODES" = 0 ]]; then
  NUM_NODES=$(clssh $TARGET_HOST cat /etc/hosts | grep ^10 | wc -l)
fi

check_deps
mkdir -p "${OUTPUT_DIRECTORY}/reports"
run_experiments

${DISTBENCH_SRC}/homa_experiments/cp_node_analysis.py \
    --directory="${OUTPUT_DIRECTORY}" \
    --workload="${WORKLOAD}" \
    --gbps="${GBPS}"

evince "${OUTPUT_DIRECTORY}/reports/test_rtts_p99_${WORKLOAD}.pdf"
