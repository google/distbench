load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

package(
    default_visibility = ["//visibility:public"],
)

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
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
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
    name = "distbench_utils",
    srcs = [
        "distbench_utils.cc",
    ],
    hdrs = [
        "distbench_utils.h",
    ],
    deps = [
        ":distbench_cc_proto",
        ":distbench_netutils",
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
    copts = select({
        ":with_mercury": ["-DWITH_MERCURY=1"],
        "//conditions:default": [],
    }) + select({
        ":with_homa": ["-DWITH_HOMA=1"],
        "//conditions:default": [],
    }) + select({
        ":with_homa_grpc": ["-DWITH_HOMA_GRPC=1"],
        "//conditions:default": [],
    }),
    deps = [
        ":composable_rpc_counter",
        ":distbench_cc_proto",
        ":protocol_driver_allocator_api",
        ":protocol_driver_api",
        ":protocol_driver_double_barrel",
        ":protocol_driver_grpc",
    ] + select({
        "with_homa": [":protocol_driver_homa"],
        "//conditions:default": [],
    }) + select({
        "with_mercury": [":protocol_driver_mercury"],
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
    copts = select({
        ":with_homa_grpc": ["-DWITH_HOMA_GRPC=1"],
        "//conditions:default": [],
    }),
    deps = [
        ":distbench_cc_grpc_proto",
        ":distbench_threadpool_lib",
        ":distbench_utils",
        ":grpc_wrapper",
        ":protocol_driver_api",
    ] + select({
        "with_homa_grpc": ["@grpc_homa//:homa_lib"],
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
        ":distbench_utils",
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
    tags = [
        "manual",
    ],
    deps = [
        ":distbench_utils",
        ":protocol_driver_api",
        "@homa_module//:homa_api",
        "@homa_module//:homa_receiver",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "gtest_utils",
    hdrs = [
        "gtest_utils.h",
    ],
)

cc_test(
    name = "protocol_driver_test",
    size = "medium",
    srcs = ["protocol_driver_test.cc"],
    copts = select({
        ":with_mercury": ["-DWITH_MERCURY=1"],
        "//conditions:default": [],
    }) + select({
        ":with_homa": ["-DWITH_HOMA=1"],
        "//conditions:default": [],
    }) + select({
        ":with_homa_grpc": ["-DWITH_HOMA_GRPC=1"],
        "//conditions:default": [],
    }),
    shard_count = 8,
    deps = [
        ":distbench_utils",
        ":gtest_utils",
        ":grpc_wrapper",
        ":protocol_driver_allocator",
        ":protocol_driver_allocator_api",
        "@com_google_googletest//:gtest_main",
        "@com_google_benchmark//:benchmark",
        "@com_github_google_glog//:glog"
    ] + select({
        "with_homa": [":protocol_driver_homa"],
        "//conditions:default": [],
    }),
)

cc_binary(
    name = "protocol_driver_benchmark",
    srcs = ["protocol_driver_test.cc"],
    deps = [
        ":distbench_utils",
        ":grpc_wrapper",
        ":gtest_utils",
        ":protocol_driver_allocator",
        "@com_google_googletest//:gtest",
        "@com_google_benchmark//:benchmark_main",
        "@com_github_google_glog//:glog"
    ],
)

proto_library(
    name = "traffic_config_proto",
    srcs = ["traffic_config.proto"],
    deps = ["//randomization:distribution_proto"],
)

cc_proto_library(
    name = "traffic_config_cc_proto",
    deps = [":traffic_config_proto"],
)

proto_library(
    name = "distbench_proto",
    srcs = ["distbench.proto"],
    deps = [
        "//randomization:distribution_proto",
        ":traffic_config_proto"
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
        ":distbench_utils",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "distbench_test_sequencer_test",
    size = "medium",
    srcs = ["distbench_test_sequencer_test.cc"],
    copts = select({
        ":with_mercury": ["-DWITH_MERCURY=1"],
        "//conditions:default": [],
    }) + select({
        ":with_homa": ["-DWITH_HOMA=1"],
        "//conditions:default": [],
    }),
    shard_count = 8,
    deps = [
        ":distbench_node_manager_lib",
        ":distbench_test_sequencer_lib",
        ":distbench_utils",
        ":grpc_wrapper",
        ":gtest_utils",
        ":protocol_driver_allocator_api",
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
        "//randomization:distribution_sample_generator",
        ":activity_api",
        ":distbench_cc_grpc_proto",
        ":distbench_utils",
        ":grpc_wrapper",
        ":protocol_driver_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/random",
    ],
)

config_setting(
    name = "do_not_use_distbench_threadpool",
    values = {
        "define": "USE_DISTBENCH_THREADPOOL=no"
    }
)

cc_library(
    name = "distbench_threadpool_lib",
    srcs = ["distbench_threadpool.cc"],
    hdrs = ["distbench_threadpool.h"],
    deps = [
        ":distbench_utils",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/strings",
        "@com_github_cthreadpool//:thpool"
    ],
    copts = select({
        ":do_not_use_distbench_threadpool":[],
        "//conditions:default":["--define USE_DISTBENCH_THREADPOOL"],
    })
)

cc_test(
    name = "distbench_threadpool_test",
    size = "medium",
    srcs = ["distbench_threadpool_test.cc"],
    deps = [
        ":distbench_threadpool_lib",
        ":gtest_utils",
        "@com_google_googletest//:gtest_main",
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
        "@com_google_googletest//:gtest_main",
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
        "@com_google_googletest//:gtest_main",
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
        ":grpc_wrapper",
        ":gtest_utils",
        ":protocol_driver_allocator",
        ":protocol_driver_allocator_api",
        "@com_google_googletest//:gtest_main",
        "@com_google_benchmark//:benchmark",
        "@com_github_google_glog//:glog"
    ],
)

cc_library(
    name = "activity_api",
    srcs = ["activity.cc"],
    hdrs = ["activity.h"],
    deps = [
        ":distbench_cc_proto",
        ":distbench_utils",
        ":traffic_config_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/random",
        "@boost//:preprocessor",
    ],
)
