cc_library(
    name = "mercury",
    hdrs = glob(["**/*.h", "**/*.tcc"]),
    strip_include_prefix = "include/",
    includes = ["include/"],
    visibility = ["//visibility:public"],
    deps = [
      ":mercury_static",
      ":mercury_na_static",
      ":mercury_util_static",
      # TODO(oserres) visibility issue : "@libfabric//:libfabric_static",
      "@libfabric//:libfabric",
    ],
    defines = [
        "WITH_MERCURY",
    ],
    linkopts = ["-lm","-lpthread"],
)

cc_import(
    name = "mercury_static",
    static_library = "lib/libmercury.a",
)

cc_import(
    name = "mercury_hl_static",
    static_library = "lib/libmercury_hl.a",
)

cc_import(
    name = "mercury_util_static",
    static_library = "lib/libmercury_util.a",
)

cc_import(
    name = "mercury_na_static",
    static_library = "lib/libna.a",
)

cc_import(
    name = "mercury_mchecksum_static",
    static_library = "lib/libmchecksum.a",
)
