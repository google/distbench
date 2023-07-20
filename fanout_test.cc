// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "distbench_node_manager.h"
#include "distbench_test_sequencer.h"
#include "distbench_test_sequencer_tester.h"
#include "distbench_thread_support.h"
#include "distbench_utils.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "protocol_driver_allocator.h"

namespace distbench {

TEST(Fanout, round_robin) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
  }
  services {
    name: "server"
    count: 4
    protocol_driver_options_name: "loopback_pd"
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
    fanout_filter: "round_robin"
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_iteration_count: 1024
    }
  }
  action_lists {
    name: "client_server_rpc"
  }
  protocol_driver_options {
    name: "loopback_pd"
    netdev_name: "lo"
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());
  auto maybe_results = tester.RunTestSequence(*test_sequence, 15);
  ASSERT_OK(maybe_results.status());
  auto& test_results = maybe_results.value().test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);

  auto serv_log_it =
      test_results.service_logs().instance_logs().find("client/0");
  for (int i = 0; i < 4; i++) {
    auto peer_log_it =
        serv_log_it->second.peer_logs().find(absl::StrCat("server/", i));
    auto rpc_log_it = peer_log_it->second.rpc_logs().find(0);
    ASSERT_EQ(rpc_log_it->second.successful_rpc_samples_size(), 256);
  }
}

TEST(Fanout, stochastic) {
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());

  const std::string proto = R"(
tests {
  services {
    name: "client"
    count: 1
  }
  services {
    name: "server"
    count: 5
  }
  rpc_descriptions {
    name: "client_server_rpc"
    client: "client"
    server: "server"
    request_payload_name: "request_payload"
    response_payload_name: "response_payload"
    fanout_filter: "stochastic{0.7:1,0.3:4}"
  }
  payload_descriptions {
    name: "request_payload"
    size: 196
  }
  payload_descriptions {
    name: "response_payload"
    size: 262144
  }
  action_lists {
    name: "client"
    action_names: "run_queries"
  }
  actions {
    name: "run_queries"
    rpc_name: "client_server_rpc"
    iterations {
      max_iteration_count: 1000
    }
  }
  action_lists {
    name: "client_server_rpc"
  }
})";
  auto test_sequence = ParseTestSequenceTextProto(proto);
  ASSERT_TRUE(test_sequence.ok());
  auto maybe_results = tester.RunTestSequence(*test_sequence, 75);
  ASSERT_OK(maybe_results.status());
  auto& test_results = maybe_results.value().test_results(0);
  ASSERT_EQ(test_results.service_logs().instance_logs_size(), 1);

  const auto& log_summary = test_results.log_summary();
  const auto& latency_summary = log_summary[1];
  size_t pos = latency_summary.find("N: ") + 3;
  ASSERT_NE(pos, std::string::npos);
  const std::string N_value = latency_summary.substr(pos);

  std::string N_value2 = N_value.substr(0, N_value.find(' '));
  int N;
  ASSERT_EQ(absl::SimpleAtoi(N_value2, &N), true);
  ASSERT_LE(N, 2300);
  ASSERT_GE(N, 1500);
}

TestSequence GetFanoutConfig(std::string fanout_filter, std::string from,
                             std::string to) {
  TestSequence test_sequence;
  auto* test = test_sequence.add_tests();

  auto* lo_opts = test->add_protocol_driver_options();
  lo_opts->set_name("lo_opts");
  lo_opts->set_netdev_name("lo");

  auto* s1 = test->add_services();
  s1->set_name(from);
  s1->set_count(27);
  s1->set_x_size(3);
  s1->set_y_size(3);
  s1->set_z_size(3);
  s1->set_protocol_driver_options_name("lo_opts");

  if (from != to) {
    auto* s2 = test->add_services();
    s2->set_name(to);
    s2->set_count(27);
    s2->set_x_size(3);
    s2->set_y_size(3);
    s2->set_z_size(3);
    s2->set_protocol_driver_options_name("lo_opts");
  }

  auto* l1 = test->add_action_lists();
  l1->set_name(from);
  l1->add_action_names("clique_queries");

  auto a1 = test->add_actions();
  a1->set_name("clique_queries");
  a1->set_rpc_name("clique_query");

  auto* r1 = test->add_rpc_descriptions();
  r1->set_name("clique_query");
  r1->set_client(from);
  r1->set_server(to);
  r1->set_fanout_filter(fanout_filter);

  auto* l2 = test->add_action_lists();
  l2->set_name("clique_query");
  return test_sequence;
}

class RpcTracker {
 public:
  RpcTracker(TestResult test_result, std::string_view from,
             std::string_view to) {
    auto& config = test_result.traffic_config();
    from_service_ = GetServiceSpec(from, config).value();
    to_service_ = GetServiceSpec(to, config).value();
    int from_size = from_service_.count();
    int to_size = to_service_.count();
    rpcs_seen_.resize(from_size, std::vector<bool>(to_size, false));
    for (auto& [name, log] : test_result.service_logs().instance_logs()) {
      int from =
          GetInstanceFromGridIndex(from_service_, GetGridIndexFromName(name));
      for (auto& [peer_name, unused] : log.peer_logs()) {
        int to = GetInstanceFromGridIndex(to_service_,
                                          GetGridIndexFromName(peer_name));
        rpcs_seen_[from][to] = true;
      }
    }
  }

  bool RpcSeen(GridIndex source, GridIndex dest) {
    int from = GetInstanceFromGridIndex(from_service_, source);
    int to = GetInstanceFromGridIndex(to_service_, dest);
    return rpcs_seen_[from][to];
  }

 private:
  ServiceSpec from_service_;
  ServiceSpec to_service_;
  std::vector<std::vector<bool>> rpcs_seen_;
};

void FanoutHelper(
    std::string fanout_filter, std::string from, std::string to,
    std::function<bool(GridIndex from, GridIndex to)> fanout_checker) {
  auto config = GetFanoutConfig(fanout_filter, from, to);
  DistBenchTester tester;
  ASSERT_OK(tester.Initialize());
  auto maybe_results = tester.RunTestSequence(config, 120);
  ASSERT_OK(maybe_results.status());
  RpcTracker tracker(maybe_results.value().test_results(0), from, to);

  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 3; ++k) {
        for (int l = 0; l < 3; ++l) {
          for (int m = 0; m < 3; ++m) {
            for (int n = 0; n < 3; ++n) {
              GridIndex from = {i, j, k};
              GridIndex to = {l, m, n};
              EXPECT_EQ(tracker.RpcSeen(from, to), fanout_checker(from, to))
                  << absl::StrFormat("{%d %d %d} vs {%d %d %d} did not match",
                                     from.x, from.y, from.z, to.x, to.y, to.z);
            }
          }
        }
      }
    }
  }
}

TEST(Fanout, all) {
  auto fanout_checker = [](GridIndex from, GridIndex to) { return from != to; };
  FanoutHelper("all", "clique", "clique", fanout_checker);
}

TEST(Fanout, same_x) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from != to && from.x == to.x;
  };
  FanoutHelper("same_x", "clique", "clique", fanout_checker);
}

TEST(Fanout, same_y) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from != to && from.y == to.y;
  };
  FanoutHelper("same_y", "clique", "clique", fanout_checker);
}

TEST(Fanout, same_z) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from != to && from.z == to.z;
  };
  FanoutHelper("same_z", "clique", "clique", fanout_checker);
}

TEST(Fanout, same_xy) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from != to && from.x == to.x && from.y == to.y;
  };
  FanoutHelper("same_xy", "clique", "clique", fanout_checker);
}

TEST(Fanout, same_xz) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from != to && from.x == to.x && from.z == to.z;
  };
  FanoutHelper("same_xz", "clique", "clique", fanout_checker);
}

TEST(Fanout, same_yz) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from != to && from.z == to.z && from.y == to.y;
  };
  FanoutHelper("same_yz", "clique", "clique", fanout_checker);
}

TEST(Fanout, same_xyz) {
  auto fanout_checker = [](GridIndex from, GridIndex to) { return from == to; };
  FanoutHelper("same_xyz", "clique", "clique2", fanout_checker);
}

TEST(Fanout, ring_x) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from.y == to.y && from.z == to.z && (from.x + 1) % 3 == to.x;
  };
  FanoutHelper("ring_x", "clique", "clique", fanout_checker);
}

TEST(Fanout, ring_y) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from.x == to.x && from.z == to.z && (from.y + 1) % 3 == to.y;
  };
  FanoutHelper("ring_y", "clique", "clique", fanout_checker);
}

TEST(Fanout, ring_z) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from.x == to.x && from.y == to.y && (from.z + 1) % 3 == to.z;
  };
  FanoutHelper("ring_z", "clique", "clique", fanout_checker);
}

TEST(Fanout, alternating_ring_x) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    if (from.y & 0x1)
      return from.y == to.y && from.z == to.z && (to.x + 1) % 3 == from.x;
    else
      return from.y == to.y && from.z == to.z && (from.x + 1) % 3 == to.x;
  };
  FanoutHelper("alternating_ring_x", "clique", "clique", fanout_checker);
}

TEST(Fanout, alternating_ring_y) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    if (from.x & 0x1)
      return from.x == to.x && from.z == to.z && (to.y + 1) % 3 == from.y;
    else
      return from.x == to.x && from.z == to.z && (from.y + 1) % 3 == to.y;
  };
  FanoutHelper("alternating_ring_y", "clique", "clique", fanout_checker);
}

TEST(Fanout, alternating_ring_z) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    if (from.x & 0x1)
      return from.x == to.x && from.y == to.y && (to.z + 1) % 3 == from.z;
    else
      return from.x == to.x && from.y == to.y && (from.z + 1) % 3 == to.z;
  };
  FanoutHelper("alternating_ring_z", "clique", "clique", fanout_checker);
}

TEST(Fanout, linear_x) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from.y == to.y && from.z == to.z && (from.x + 1) % 3 == to.x &&
           from.x < 2;
  };
  FanoutHelper("linear_x{1}", "clique", "clique", fanout_checker);
}

TEST(Fanout, linear_y) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from.x == to.x && from.z == to.z && (from.y + 1) % 3 == to.y &&
           from.y < 2;
  };
  FanoutHelper("linear_y{1}", "clique", "clique", fanout_checker);
}

TEST(Fanout, linear_z) {
  auto fanout_checker = [](GridIndex from, GridIndex to) {
    return from.x == to.x && from.y == to.y && (from.z + 1) % 3 == to.z &&
           from.z < 2;
  };
  FanoutHelper("linear_z{1}", "clique", "clique", fanout_checker);
}
}  // namespace distbench
