#!/bin/bash

echo Building static Distbench binary
(
	cd ..
	bazel build --cxxopt='-std=c++17' --linkopt='--static -fPIC'  :distbench
)

echo Running playbook
ansible-playbook playbook.yml
