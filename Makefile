default: test all

test:
	bazel test --cxxopt='-std=c++17' :all

all:
	bazel build --cxxopt='-std=c++17' :all

clean:
	bazel clean
