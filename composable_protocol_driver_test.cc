// Copyright 2021 Google LLC
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

#include "benchmark/benchmark.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "protocol_driver_allocator.h"

namespace distbench {

ProtocolDriverOptions DoubleBarrelRpcCounterGrpc() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("double_barrel");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "inline");
  AddServerStringOptionTo(pdo, "next_protocol_driver",
                          "composable_rpc_counter");
  AddServerStringOptionTo(pdo, "driver_under_test", "grpc");
  return pdo;
}

class ComposableProtocolDriverTest : public ::testing::Test {};

TEST_F(ComposableProtocolDriverTest, RpcCounterTest) {
  ProtocolDriverOptions pdo = DoubleBarrelRpcCounterGrpc();
  int port = 0;
  auto maybe_pd = AllocateProtocolDriver(pdo, &port);
  ASSERT_OK(maybe_pd.status());
  auto& pd = maybe_pd.value();
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  pd->SetHandler([&](ServerRpcState* s) {
    ++server_rpc_count;
    if (s->have_dedicated_thread) {
      std::string str;
      s->request->SerializeToString(&str);
      s->SendResponseIfSet();
      s->FreeStateIfSet();
      return std::function<void()>();
    } else {
      std::function<void()> fct = [=]() {
        usleep(100'000);
        std::string str;
        s->request->SerializeToString(&str);
        s->SendResponseIfSet();
        s->FreeStateIfSet();
      };
      return fct;
    }
  });
  std::string addr = pd->HandlePreConnect("", 0).value();
  ASSERT_OK(pd->HandleConnect(addr, 0));

  std::atomic<int> client_rpc_count = 0;
  const int kNumIterations = 1000;
  ClientRpcState rpc_state[kNumIterations];
  for (int i = 0; i < kNumIterations; ++i) {
    pd->InitiateRpc(0, &rpc_state[i], [&, i]() {
      if (rpc_state[i].success) ++client_rpc_count;
    });
  }
  pd->ShutdownClient();

  if (pdo.protocol_name() == "double_barrel") {
    std::map<std::string, int> expected_transport_stats = {
        {"instance_1/client_rpc_cnt", kNumIterations / 2},
        {"instance_1/server_rpc_cnt", kNumIterations},
        {"instance_2/client_rpc_cnt", kNumIterations / 2},
        {"instance_2/server_rpc_cnt", 0}};
    auto transport_stats = pd->GetTransportStats();
    EXPECT_EQ(transport_stats.size(), 4);
    for (const auto& stat : transport_stats) {
      EXPECT_EQ(stat.value, expected_transport_stats[stat.name]);
    }
  }
  EXPECT_EQ(server_rpc_count, kNumIterations);
  EXPECT_EQ(client_rpc_count, kNumIterations);
}

// clang-format on

}  // namespace distbench
