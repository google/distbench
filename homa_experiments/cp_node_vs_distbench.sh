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
  exit;
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
  exit;
fi

if [ ${#EXPERIMENTS[@]} -ne 0 ]
then
EXPERIMENTS=("homa", "grpc")
fi

function run_cp_node {
  echo "Running cp_node experiments";
  ssh -o 'StrictHostKeyChecking no' \
    "${DNS}" \
    bash  << EOF
  #This runs on the remote system:
  homaModule/util/cp_vs_tcp -n ${NUMBER_OF_NODES} -w ${WORKLOAD} --client-ports 1 --server-ports 1 -s ${TEST_DURATION}\
  --tcp-client-ports 1 --port-threads 1 -b ${GBPS} -l ~/logs;
EOF

  scp ${DNS}:~/logs/*.rtts "${OUTPUT_DIRECTORY}";
  cat ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}-*.rtts > ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}.rtts
  cat ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}-*.rtts > ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}.rtts
  rm ${OUTPUT_DIRECTORY}/homa_${WORKLOAD}-*.rtts
  rm ${OUTPUT_DIRECTORY}/tcp_${WORKLOAD}-*.rtts
}

function run_distbench { 
  echo "running distbench experiments"
  IFS='@'
  user_hostname=(${DNS})
  unset IFS;

  TEMPDIR=$(mktemp -d)
  NEW_EXPERIMENTS="${EXPERIMENTS[@]%,}"
  for exp in ${NEW_EXPERIMENTS[@]} 
  do
    ~/distbench/test_builder/test_builder "homa_cp_node:${exp}" \
    -c gbps=${GBPS}:workload=${WORKLOAD}:node_count=${NUMBER_OF_NODES}:test_duration=${TEST_DURATION} \
    -o ${OUTPUT_DIRECTORY} -s localhost:11000;
  done

  for f in ${OUTPUT_DIRECTORY}/*.pb.gz;
  do
    case $(basename ${f}) in
    *-homa.pb.gz)
      ~/distbench/bazel-bin/analysis/results_conversion --input_file=${f} --output_directory=${TEMPDIR} --supress_header;
      mv ${TEMPDIR}/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_homa_${WORKLOAD}.rtts --output_format=homa;;

    *-grpc_polling_inline.pb.gz) 
      ~/distbench/bazel-bin/analysis/results_conversion --input_file=${f} --output_directory=${TEMPDIR} --supress_header;
      mv ${TEMPDIR}/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_grpc_polling_${WORKLOAD}.rtts --output_format=homa;;

    *-grpc_polling_polling.pb.gz) 
      ~/distbench/bazel-bin/analysis/results_conversion --input_file=${f} --output_directory=${TEMPDIR} --supress_header;
      mv ${TEMPDIR}/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_grpc_inline_${WORKLOAD}.rtts --output_format=homa;;

    *-grpc_polling_handoff.pb.gz) 
      ~/distbench/bazel-bin/analysis/results_conversion --input_file=${f} --output_directory=${TEMPDIR} --supress_header;
      mv ${TEMPDIR}/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_grpc_handoff_${WORKLOAD}.rtts --output_format=homa;;

    *-grpc_polling_inline_homa.pb.gz) 
      ~/distbench/bazel-bin/analysis/results_conversion --input_file=${f} --output_directory=${TEMPDIR} --supress_header;
      mv ${TEMPDIR}/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_grpc_polling_homa_${WORKLOAD}.rtts --output_format=homa;;

    *-grpc_polling_polling_homa.pb.gz) 
      ~/distbench/bazel-bin/analysis/results_conversion --input_file=${f} --output_directory=${TEMPDIR} --supress_header;
      mv ${TEMPDIR}/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_grpc_inline_homa_${WORKLOAD}.rtts --output_format=homa;;

    *-grpc_polling_handoff_homa.pb.gz) 
      ~/distbench/bazel-bin/analysis/results_conversion --input_file=${f} --output_directory=${TEMPDIR} --supress_header;
      mv ${TEMPDIR}/overall_summary.txt ${OUTPUT_DIRECTORY}/distbench_grpc_handoff_homa_${WORKLOAD}.rtts --output_format=homa;;
    esac      
  done
}

run_cp_node
run_distbench