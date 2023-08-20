package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "homa_api",
    srcs = [
        "homa_api.c",
    ],
    hdrs = [
        "homa.h",
    ],
)

cc_library(
    name = "homa_receiver",
    srcs = [
        "homa_receiver.cc",
    ],
    hdrs = [
        "homa_receiver.h",
    ],
    deps = [
        ":homa_api"
    ],
)

cc_binary(
  name = "dist_to_proto",
  srcs = [ "util/dist_to_proto.cc", "util/dist.cc", "util/dist.h", "homa.h" ],
)
