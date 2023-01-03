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
