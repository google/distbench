#!/bin/bash

echo Build and Upload the Distbench Docker image
echo

echo "${CLOUD_ENGINE_PROJECT}"

if [ -z "${CLOUD_ENGINE_PROJECT}" ] 
then
	echo CLOUD_ENGINE_PROJECT is undefined
	echo Set CLOUD_ENGINE_PROJECT to your cloud engine project name
	echo It will be used to tag and push the image
	exit 1
fi

(
	cd ..
	bazel build --cxxopt='-std=c++17' --linkopt='--static -fPIC'  :distbench
)
rm -f distbench
cp ../bazel-bin/distbench .
docker build . --tag  gcr.io/$CLOUD_ENGINE_PROJECT/distbench
docker push gcr.io/$CLOUD_ENGINE_PROJECT/distbench

