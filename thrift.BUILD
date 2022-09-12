cc_library(
    name = "thrift",
    hdrs = glob(["**/*.h", "**/*.tcc"]),
    strip_include_prefix = "include/",
    includes = ["include/"],
    visibility = ["//visibility:public"],
    deps = [":thrift_static"],
    linkopts = ["-lm","-lpthread"],
)

cc_import(
    name = "thrift_static",
    static_library = "lib/libthrift.a",
)
