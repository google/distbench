#!/bin/bash

cat deployment_test_sequencer.yaml | sed -e 's/replicas: 1/replicas: 0/' | envsubst '$CLOUD_ENGINE_PROJECT' | kubectl apply -f -
cat deployment_node_managers.yaml  | sed -e 's/replicas: 1/replicas: 0/' | envsubst '$CLOUD_ENGINE_PROJECT' | kubectl apply -f -

