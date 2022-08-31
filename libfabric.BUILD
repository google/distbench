cc_library(
    name = "libfabric",
    hdrs = glob(["**/*.h", "**/*.tcc"]),
    strip_include_prefix = "include/",
    includes = ["include/"],
    visibility = ["//visibility:public"],
    deps = [":libfabric_static"],
    linkopts = [
        "-lm",
        "-lnuma",
        "-luuid",
        "-lpthread",
        "-lrt",
        "-ldl",
        "-latomic",
    ],
)

cc_import(
    name = "libfabric_static",
    static_library = "lib/libfabric.a",
)
