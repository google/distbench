sh_test(
    name = "results_conversion_test",
    srcs = ["results_conversion_test.sh"],
    data = glob(["golden_results/**"]) + [":results_conversion"],
)

py_binary(
    name = "results_conversion",
    srcs = ["results_conversion.py"],
    deps = [
        "//:joint_distribution_py_proto",
        "//:distbench_py_proto",
        "//:traffic_config_py_proto",
    ],
)
