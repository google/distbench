# Generate random samples from the supplied distribution.

package(
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "distribution_proto",
    srcs = ["distribution.proto"],
)

cc_proto_library(
    name = "distribution_cc_proto",
    deps = [":distribution_proto"],
)

cc_library(
    name = "distribution_sample_generator",
    srcs = ["distribution_sample_generator.cc"],
    hdrs = ["distribution_sample_generator.h"],
    deps = [
        ":distribution_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/random",
        "@com_github_google_glog//:glog"
    ],
)

cc_test(
    name = "distribution_sample_generator_test",
    srcs = ["distribution_sample_generator_test.cc"],
    deps = [
        ":distribution_sample_generator",
        "@com_google_googletest//:gtest_main",
    ],
)
