load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

package(
    default_visibility = ["//visibility:public"],
)

# Declare flags that can be set on the build/test command line,
# e.g. bazel build :all --//:with-homa=true --//:with-homa-grpc=true
bool_flag(
    name = "with-mercury",
    build_setting_default = False,
)

bool_flag(
    name = "with-homa",
    build_setting_default = False,
)

bool_flag(
    name = "with-homa-grpc",
    build_setting_default = False,
)

# Declare build settings that propgate flags values to build rules:
config_setting(
    name = "with_mercury",
    flag_values = {":with-mercury": "True"},
)

config_setting(
    name = "with_homa",
    flag_values = {":with-homa": "True"},
)

config_setting(
    name = "with_homa_grpc",
    flag_values = {":with-homa-grpc": "True"},
)

cc_library(
    name = "distbench_netutils",
    srcs = [
        "distbench_netutils.cc",
    ],
    hdrs = [
        "distbench_netutils.h",
    ],
    deps = [
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_github_google_glog//:glog"
    ],
)

cc_library(
    name = "interface_lookup",
    srcs = [
        "interface_lookup.cc",
    ],
    hdrs = [
        "interface_lookup.h",
    ],
)

cc_library(
    name = "distbench_summary",
    srcs = [
        "distbench_summary.cc",
    ],
    hdrs = [
        "distbench_summary.h",
    ],
    deps = [
        ":distbench_cc_proto",
        ":traffic_config_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_github_google_glog//:glog"
    ],
)

cc_library(
    name = "grpc_wrapper",
    hdrs = [
        "grpc_wrapper.h",
    ],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",
    ],
)

cc_library(
    name = "distbench_thread_support",
    srcs = [
        "distbench_thread_support.cc",
    ],
    hdrs = [
        "distbench_thread_support.h",
    ],
)

cc_library(
    name = "distbench_utils",
    srcs = [
        "distbench_utils.cc",
    ],
    hdrs = [
        "distbench_utils.h",
    ],
    deps = [
        ":distbench_cc_proto",
        ":grpc_wrapper",
        ":interface_lookup",
        ":traffic_config_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_github_google_glog//:glog"
    ],
)

cc_library(
    name = "protocol_driver_api",
    srcs = [
        "protocol_driver.cc",
    ],
    hdrs = [
        "protocol_driver.h",
    ],
    deps = [
        ":distbench_cc_proto",
        ":distbench_utils",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_library(
    name = "protocol_driver_allocator_api",
    hdrs = [
        "protocol_driver_allocator.h",
    ],
    deps = [
        ":distbench_cc_proto",
        ":protocol_driver_api",
    ],
)

cc_library(
    name = "protocol_driver_allocator",
    srcs = [
        "protocol_driver_allocator.cc",
    ],
    deps = [
        ":composable_rpc_counter",
        ":distbench_cc_proto",
        ":protocol_driver_allocator_api",
        ":protocol_driver_api",
        ":protocol_driver_double_barrel",
        ":protocol_driver_grpc",
    ] + select({
        ":with_homa": [":protocol_driver_homa"],
        "//conditions:default": [],
    }) + select({
        ":with_mercury": [":protocol_driver_mercury"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "protocol_driver_grpc",
    srcs = [
        "protocol_driver_grpc.cc",
    ],
    hdrs = [
        "protocol_driver_grpc.h",
    ],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_netutils",
        ":distbench_threadpool_lib",
        ":grpc_wrapper",
        ":protocol_driver_api",
    ] + select({
        ":with_homa_grpc": ["@grpc_homa//:homa_lib"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "protocol_driver_mercury",
    srcs = [
        "protocol_driver_mercury.cc",
    ],
    hdrs = [
        "protocol_driver_mercury.h",
    ],
    tags = [
        "manual",
    ],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_netutils",
        ":distbench_thread_support",
        ":protocol_driver_api",
        "@mercury//:mercury",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "protocol_driver_homa",
    srcs = [
        "protocol_driver_homa.cc",
    ],
    hdrs = [
        "protocol_driver_homa.h",
    ],
    defines = [
        "WITH_HOMA",
    ],
    tags = [
        "manual",
    ],
    deps = [
        ":distbench_netutils",
        ":distbench_thread_support",
        ":distbench_utils",
        ":protocol_driver_api",
        "@homa_module//:homa_api",
        "@homa_module//:homa_receiver",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "gtest_utils",
    srcs = ["gtest_utils.cc"],
    hdrs = [
        "gtest_utils.h",
    ],
    deps = [
        "@com_google_googletest//:gtest",
        "@com_github_google_glog//:glog"
    ],
)

cc_library(
    name = "benchmark_utils",
    srcs = ["benchmark_utils.cc"],
    deps = [
        "@com_google_benchmark//:benchmark",
        "@com_github_google_glog//:glog"
    ],
)

cc_test(
    name = "protocol_driver_test",
    size = "medium",
    srcs = ["protocol_driver_test.cc"],
    shard_count = 8,
    deps = [
        ":distbench_utils",
        ":gtest_utils",
        ":protocol_driver_allocator",
        ":protocol_driver_allocator_api",
        "@com_github_google_glog//:glog"
    ] + select({
        ":with_homa": [":protocol_driver_homa"],
        "//conditions:default": [],
    }),
)

cc_binary(
    name = "protocol_driver_benchmark",
    srcs = ["protocol_driver_benchmark.cc"],
    deps = [
        ":distbench_utils",
        ":benchmark_utils",
        ":protocol_driver_allocator",
        "@com_google_googletest//:gtest",
        "@com_github_google_glog//:glog"
    ],
)

proto_library(
    name = "traffic_config_proto",
    srcs = ["traffic_config.proto"],
    deps = [":joint_distribution_proto"],
)

cc_proto_library(
    name = "traffic_config_cc_proto",
    deps = [":traffic_config_proto"],
)

proto_library(
    name = "distbench_proto",
    srcs = ["distbench.proto"],
    deps = [
        ":joint_distribution_proto",
        ":traffic_config_proto",
    ],
)

cc_proto_library(
    name = "distbench_cc_proto",
    deps = [":distbench_proto"],
)

cc_grpc_library(
    name = "distbench_cc_grpc_proto",
    srcs = [":distbench_proto"],
    deps = [":distbench_cc_proto"],
    grpc_only = True,
)

cc_library(
    name = "distbench_test_sequencer_lib",
    srcs = ["distbench_test_sequencer.cc"],
    hdrs = ["distbench_test_sequencer.h"],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_summary",
        ":distbench_netutils",
        ":distbench_utils",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "distbench_test_sequencer_test",
    size = "medium",
    srcs = ["distbench_test_sequencer_test.cc"],
    shard_count = 8,
    deps = [
        ":distbench_node_manager_lib",
        ":distbench_test_sequencer_lib",
        ":distbench_utils",
        ":grpc_wrapper",
        ":gtest_utils",
        ":protocol_driver_allocator_api",
    ],
)

cc_library(
    name = "distbench_node_manager_lib",
    srcs = ["distbench_node_manager.cc"],
    hdrs = ["distbench_node_manager.h"],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_engine_lib",
        ":distbench_netutils",
        ":grpc_wrapper",
        ":protocol_driver_allocator",
        ":protocol_driver_allocator_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "distbench_engine_lib",
    srcs = ["distbench_engine.cc"],
    hdrs = ["distbench_engine.h"],
    deps = [
        ":activity_api",
        ":distbench_cc_grpc_proto",
        ":distbench_netutils",
        ":distbench_thread_support",
        ":distbench_threadpool_lib",
        ":joint_distribution_sample_generator",
        ":grpc_wrapper",
        ":protocol_driver_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/random",
    ],
)

cc_library(
    name = "distbench_threadpool_lib",
    srcs = ["distbench_threadpool.cc"],
    hdrs = ["distbench_threadpool.h"],
    deps = [
        ":distbench_thread_support",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/strings",
    ] + select({
        ":with_mercury": ["@mercury//:mercury"],
        "//conditions:default": [],
    })
)

cc_test(
    name = "distbench_threadpool_test",
    size = "small",
    srcs = ["distbench_threadpool_test.cc"],
    deps = [
        ":distbench_threadpool_lib",
        ":gtest_utils",
    ],
)

cc_binary(
    name = "distbench_threadpool_benchmark",
    srcs = ["distbench_threadpool_benchmark.cc"],
    deps = [
        ":distbench_threadpool_lib",
        ":benchmark_utils",
    ],
)

cc_test(
    name = "distbench_engine_test",
    size = "medium",
    srcs = ["distbench_engine_test.cc"],
    deps = [
        ":distbench_engine_lib",
        ":distbench_utils",
        ":gtest_utils",
        ":protocol_driver_allocator",
    ],
)

cc_test(
    name = "distbench_node_manager_test",
    size = "medium",
    srcs = ["distbench_node_manager_test.cc"],
    deps = [
        ":distbench_node_manager_lib",
        ":distbench_utils",
        ":gtest_utils",
        ":protocol_driver_allocator_api",
    ],
)

cc_binary(
    name = "distbench",
    srcs = ["distbench_busybox.cc"],
    deps = [
        ":distbench_node_manager_lib",
        ":distbench_test_sequencer_lib",
        ":distbench_utils",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_library(
    name = "protocol_driver_double_barrel",
    srcs = [
        "protocol_driver_double_barrel.cc",
    ],
    hdrs = [
        "protocol_driver_double_barrel.h",
    ],
    deps = [
        ":distbench_utils",
        ":protocol_driver_allocator_api",
        ":protocol_driver_api",
    ],
)

cc_library(
    name = "composable_rpc_counter",
    srcs = [
        "composable_rpc_counter.cc",
    ],
    hdrs = [
        "composable_rpc_counter.h",
    ],
    deps = [
        ":distbench_utils",
        ":protocol_driver_allocator_api",
        ":protocol_driver_api",
    ],
)

cc_test(
    name = "composable_protocol_driver_test",
    size = "medium",
    srcs = ["composable_protocol_driver_test.cc"],
    shard_count = 8,
    deps = [
        ":distbench_utils",
        ":gtest_utils",
        ":protocol_driver_allocator",
        ":protocol_driver_allocator_api",
        "@com_github_google_glog//:glog"
    ],
)

cc_test(
    name = "activity_test",
    size = "medium",
    srcs = ["activity_test.cc"],
    shard_count = 8,
    deps = [
        ":distbench_node_manager_lib",
        ":distbench_test_sequencer_lib",
        ":distbench_utils",
        ":grpc_wrapper",
        ":gtest_utils",
        ":protocol_driver_allocator_api",
    ],
)

cc_library(
    name = "activity_api",
    srcs = ["activity.cc"],
    hdrs = ["activity.h"],
    deps = [
        ":distbench_cc_proto",
        ":distbench_utils",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/random",
        "@boost//:preprocessor",
    ],
)

proto_library(
    name = "joint_distribution_proto",
    srcs = ["joint_distribution.proto"],
)

cc_proto_library(
    name = "joint_distribution_cc_proto",
    deps = [":joint_distribution_proto"],
)

cc_library(
    name = "joint_distribution_sample_generator",
    srcs = ["joint_distribution_sample_generator.cc"],
    hdrs = ["joint_distribution_sample_generator.h"],
    deps = [
        ":joint_distribution_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/random",
        "@com_github_google_glog//:glog"
    ],
)

cc_test(
    name = "joint_distribution_sample_generator_test",
    srcs = ["joint_distribution_sample_generator_test.cc"],
    deps = [
        ":joint_distribution_sample_generator",
        ":gtest_utils",
    ],
)
