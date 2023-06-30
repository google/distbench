sh_test(
    name = "test_builder_test",
    srcs = ["test_builder_test.sh"],
    data = glob(["test_builder_golden_configs/*"]) +
           ["test_builder", "//:distbench"],
)
