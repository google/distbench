#!/bin/bash
#
# you have running the distbench deploy_to_cloudlab.sh script in another terminal 
# Takes the DNS first optional argument
# -x experiments you wanna run in distbench (-x homa -x grpc -xgrpc:server_type=${SERVER_TYPE}
# -x grpc:server_type=${SERVER_TYPE}:transport=homa etc. Default: homa and default grpc)
# -n number of nodes (default=10)
# -w workload (default=w5)
# -b Gbps for the test (default=1)
# -s seconds to run the tests (default=10)
# -o output_directory

set -uEeo pipefail
shopt -s inherit_errexit

NUMBER_OF_NODES=5
WORKLOAD='w5'
GBPS=1
TEST_DURATION=10
OUTPUT_DIRECTORY=''
DNS=${1}
EXPERIMENTS=()

DISTBENCH_SRC=$(readlink -f $(dirname $(which $0))/..)

if ! which dist_to_proto &> /dev/null; then
  echo "dist_to_proto not found in path"
  echo "build and install it from https://github.com/PlatformLab/HomaModule"
  echo "It is found in the utils/ directory"
  exit 1
fi

RESULTS_CONVERSION="${DISTBENCH_SRC}/bazel-bin/analysis/results_conversion"

if [[ ! -x ${RESULTS_CONVERSION} ]]; then
  echo "$RESULTS_CONVERSION not an executable file, trying to build it"
  (cd ${DISTBENCH_SRC} && bazel  build //analysis:results_conversion) || exit 1
fi

function print_usage_and_exit {
  echo "To run an experiment you must pass as first argument the USER@HOSTNAME 
        from cloudlab and you can add the following flags:
        -x experiments you wanna run in distbench (-x homa -x grpc -xgrpc:server_type=SERVER_TYPE
        grpc:server_type=SERVER_TYPE:transport=homa etc. Default: homa and default grpc)
        -n number of nodes (default=10)
        -w workload (default=w5)
        -b Gbps for the test (default=1)
        -s seconds to run the tests (default=10)
        -o output_directory (necessary)
        The script also assumes you have deploy_to_cloudlab,sh running in another terminal
        and homa is installed in the cloudlab cluster" 
  exit
}

ALLOWED_OPTS="x:n:w:b:s:o:"
parsed_opts="$(getopt -o "${ALLOWED_OPTS}" -- "$@")" || print_usage_and_exit 1
eval set -- "${parsed_opts}"

while getopts "${ALLOWED_OPTS}" flag "$@"; do
  case "${flag}" in
    x) EXPERIMENTS+=("${OPTARG}") ;;
    n) NUMBER_OF_NODES="${OPTARG}" ;;
    w) WORKLOAD="${OPTARG}" ;;
    b) GBPS="${OPTARG}" ;;
    s) TEST_DURATION="${OPTARG}" ;;
    o) OUTPUT_DIRECTORY="${OPTARG}" ;;
    *) print_usage_and_exit;;
  esac
done

if [ ${OUTPUT_DIRECTORY} == '' ]; then
  echo "You must specify an output directory (-o)"
  exit
fi

mkdir -p "${OUTPUT_DIRECTORY}/reports"

if [ ${#EXPERIMENTS[@]} -ne 0 ]
then
EXPERIMENTS=("homa", "grpc")
fi

function run_cp_node {
  echo "Running cp_node experiments"
  ssh -o 'StrictHostKeyChecking no' \
    "${DNS}" \
    bash  << EOF
  #This runs on the remote system:
  homaModule/util/cp_vs_tcp -n ${NUMBER_OF_NODES} -w ${WORKLOAD} --client-ports 1 --server-ports 1 -s ${TEST_DURATION}\
      --tcp-client-ports 1 --tcp-server-ports=1 --port-threads 1 -b ${GBPS} -l ~/logs --port-receivers 1
EOF

  scp ${DNS}:~/logs/*.rtts "${OUTPUT_DIRECTORY}"
  cat ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}-*.rtts > ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}.rtts
  cat ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}-*.rtts > ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}.rtts
  rm ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}-*.rtts
  rm ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}-*.rtts
}

function run_distbench { 
  echo "running distbench experiments"
  IFS='@'
  user_hostname=(${DNS})
  unset IFS

  NEW_EXPERIMENTS="${EXPERIMENTS[@]%,}"
  for exp in ${NEW_EXPERIMENTS[@]} 
  do
    echo "$exp"
    TEMPDIR=$(mktemp -d)
    ${DISTBENCH_SRC}/test_builder/test_builder "homa_cp_node:${exp}" \
        -c gbps=${GBPS}:workload=${WORKLOAD}:node_count=${NUMBER_OF_NODES}:test_duration=${TEST_DURATION} \
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
  done
}

run_cp_node
run_distbench

${DISTBENCH_SRC}/homa_experiments/cp_node_analysis.py \
    --directory="${OUTPUT_DIRECTORY}" \
    --workload="${WORKLOAD}" \
    --gbps="${GBPS}"

evince "${OUTPUT_DIRECTORY}/reports/test_rtts_p99_${WORKLOAD}.pdf"
