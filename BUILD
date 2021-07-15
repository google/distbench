load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")

package(
    default_visibility = ["//visibility:public"],
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
    name = "distbench_utils",
    srcs = [
        "distbench_utils.cc",
    ],
    hdrs = [
        "distbench_utils.h",
        "grpc_wrapper.h",
    ],
    deps = [
        ":interface_lookup",
        ":distbench_cc_proto",
        ":traffic_config_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
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
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_library(
    name = "protocol_driver_allocator",
    srcs = [
        "protocol_driver_allocator.cc",
    ],
    hdrs = [
        "protocol_driver_allocator.h",
    ],
    deps = [
        ":distbench_cc_proto",
        ":protocol_driver_api",
        ":protocol_driver_grpc",
        ":protocol_driver_grpc_async_callback"
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
    name = "protocol_driver_grpc",
    srcs = [
        "protocol_driver_grpc.cc",
    ],
    hdrs = [
        "protocol_driver_grpc.h",
    ],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_utils",
        ":protocol_driver_api",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "protocol_driver_grpc_async_callback",
    srcs = [
        "protocol_driver_grpc_async_callback.cc",
    ],
    hdrs = [
        "protocol_driver_grpc_async_callback.h",
    ],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_utils",
        ":protocol_driver_api",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_google_glog//:glog"
    ],
)

cc_test(
    name = "protocol_driver_test",
    size = "medium",
    srcs = ["protocol_driver_test.cc",
        "gtest_utils.h"],
    deps = [
        ":distbench_utils",
        ":protocol_driver_allocator",
        "@com_google_googletest//:gtest_main",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_benchmark//:benchmark",
        "@com_github_google_glog//:glog"
    ],
)

cc_binary(
    name = "protocol_driver_benchmark",
    srcs = ["protocol_driver_test.cc",
        "gtest_utils.h"],
    deps = [
        ":distbench_utils",
        ":protocol_driver_allocator",
        "@com_google_googletest//:gtest",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_benchmark//:benchmark_main",
        "@com_github_google_glog//:glog"
    ],
)

proto_library(
    name = "traffic_config_proto",
    srcs = ["traffic_config.proto"],
)

cc_proto_library(
    name = "traffic_config_cc_proto",
    deps = [":traffic_config_proto"],
)

proto_library(
    name = "distbench_proto",
    srcs = ["distbench.proto"],
    deps = ["traffic_config_proto"],
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
        ":distbench_utils",
        ":distbench_summary",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",
    ],
)

cc_test(
    name = "distbench_test_sequencer_test",
    size = "medium",
    srcs = ["distbench_test_sequencer_test.cc", "gtest_utils.h"],
    deps = [
        ":distbench_node_manager_lib",
        ":distbench_test_sequencer_lib",
        ":distbench_utils",
        ":protocol_driver_allocator",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "distbench_node_manager_lib",
    srcs = ["distbench_node_manager.cc"],
    hdrs = ["distbench_node_manager.h"],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_engine_lib",
        ":distbench_utils",
        ":protocol_driver_allocator",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "distbench_engine_lib",
    srcs = ["distbench_engine.cc"],
    hdrs = ["distbench_engine.h"],
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_utils",
        ":protocol_driver_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/random",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",
    ],
)

cc_test(
    name = "distbench_engine_test",
    size = "medium",
    srcs = ["distbench_engine_test.cc", "gtest_utils.h"],
    deps = [
        ":distbench_engine_lib",
        ":distbench_utils",
        ":protocol_driver_allocator",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "distbench",
    srcs = ["distbench_busybox.cc"],
    deps = [
        ":distbench_node_manager_lib",
        ":distbench_test_sequencer_lib",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
    ],
)

