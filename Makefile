default: all

testlog:
	bazel test --test_output=all :all

test:
	bazel test :all

test_asan:
	bazel test --config=asan --test_output=errors :all

test_tsan:
	bazel test --config=tsan --test_output=errors :all

all:
	bazel build :all

clean:
	bazel clean
