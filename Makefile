default: all

testlog:
	bazel test --test_output=all :all

test:
	bazel test :all

test_with_mercury:
	bazel test :all --//:with-mercury --test_output=all
	bazel test --config=asan :all --//:with-mercury --test_output=all
	bazel test --config=tsan :all --//:with-mercury --test_output=all

test_homa:
	bazel test :all --//:with-homa --//:with-homa-grpc

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
	bazel build :all

clean:
	bazel clean
