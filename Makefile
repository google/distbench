default: all

testlog:
	bazel test --test_output=all --cxxopt='-std=c++17' :all

test:
	bazel test --cxxopt='-std=c++17' :all

all:
	bazel build --cxxopt='-std=c++17' :all

clean:
	bazel clean
