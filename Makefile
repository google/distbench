default: all

thrift_proto: distbench.thrift
	../../opt/thrift/bin/thrift --gen cpp distbench.thrift

testlog:
	bazel test --test_output=all :all

test:
	bazel test :all

test_with_mercury:
	bazel test :all --//:with-mercury --test_output=all
	bazel test --config=asan :all --//:with-mercury --test_output=all
	bazel test --config=tsan :all --//:with-mercury --test_output=all

test_asan:
	bazel test --config=asan --test_output=errors :all

test_tsan:
	bazel test --config=tsan --test_output=errors :all

basicprof:
	bazel build --config=basicprof :all
	bazel-bin/protocol_driver_test
	gprof bazel-bin/protocol_driver_test

clang-format:
	for file in *.{cc,h}; do\
		clang-format -i -style=file $${file};\
	done

all:
	bazel build :distbench

test_with_thrift: thrift_proto
	bazel test :all --//:with-thrift

all_with_thrift: thrift_proto
	bazel build :distbench --//:with-thrift

clean:
	bazel clean
