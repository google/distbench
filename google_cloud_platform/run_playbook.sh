#!/bin/bash

echo Building static Distbench binary
(
	cd ..
	bazel build -c opt --cxxopt='-std=c++17' --linkopt='--static -fPIC'  :distbench
)

echo Running playbook

export ANSIBLE_HOST_KEY_CHECKING=False
ansible-playbook playbook.yml
