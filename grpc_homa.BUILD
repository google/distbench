package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "homa_util_lib",
    srcs = [
        "util.cc",
    ],
    hdrs = [
        "util.h",
    ],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

cc_library(
    name = "homa_time_trace_lib",
    srcs = [
        "time_trace.cc",
    ],
    hdrs = [
        "time_trace.h",
    ],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

cc_library(
    name = "homa_wire_lib",
    srcs = [
        "wire.cc",
    ],
    hdrs = [
        "wire.h",
    ],
    deps = [
        ":homa_util_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

cc_library(
    name = "homa_stream_id_lib",
    srcs = [
        "stream_id.cc",
    ],
    hdrs = [
        "stream_id.h",
    ],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

cc_library(
    name = "homa_incoming_lib",
    srcs = [
        "homa_incoming.cc",
    ],
    hdrs = [
        "homa_incoming.h",
    ],
    deps = [
        ":homa_time_trace_lib",
        ":homa_socket_lib",
        ":homa_stream_id_lib",
        ":homa_wire_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

cc_library(
    name = "homa_socket_lib",
    srcs = [
        "homa_socket.cc",
    ],
    hdrs = [
        "homa_socket.h",
    ],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

cc_library(
    name = "homa_stream_lib",
    srcs = [
        "homa_stream.cc",
    ],
    hdrs = [
        "homa_stream.h",
    ],
    deps = [
        ":homa_incoming_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)


cc_library(
    name = "homa_client_lib",
    srcs = [
        "homa_client.cc",
    ],
    hdrs = [
        "homa_client.h",
    ],
    deps = [
        ":homa_stream_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

cc_library(
    name = "homa_listener_lib",
    srcs = [
        "homa_listener.cc",
    ],
    hdrs = [
        "homa_listener.h",
    ],
    deps = [
        ":homa_stream_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)


cc_library(
    name = "homa_lib",
    deps = [
        ":homa_client_lib",
        ":homa_listener_lib",
        ":homa_stream_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@homa_module//:homa_api",
    ]
)

