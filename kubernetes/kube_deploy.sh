#!bin/bash

cat deployment_test_sequencer.yaml | envsubst '$CLOUD_ENGINE_PROJECT' | kubectl apply -f -
cat deployment_node_managers.yaml  | envsubst '$CLOUD_ENGINE_PROJECT' | kubectl apply -f -
kubectl apply -f service.yaml

