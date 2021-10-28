default: all

testlog:
	bazel test --test_output=all :all

test:
	bazel test :all

test_asan:
	bazel test --config=asan --test_output=errors :all

test_tsan:
	bazel test --config=tsan --test_output=errors :all

basicprof:
	bazel build --config=basicprof :all
	bazel-bin/protocol_driver_test
	gprof bazel-bin/protocol_driver_test

all:
	bazel build :all

clean:
	bazel clean
