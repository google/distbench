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

class ProtocolDriverTest
    : public testing::TestWithParam<ProtocolDriverOptions> {};

TEST_P(ProtocolDriverTest, Allocate) {
  ProtocolDriverOptions pdo = GetParam();
  int port = 0;
  auto maybe_pd = AllocateProtocolDriver(pdo, &port);
  ASSERT_OK(maybe_pd.status());
}

TEST_P(ProtocolDriverTest, SetNumPeers) {
  ProtocolDriverOptions pdo = GetParam();
  int port = 0;
  auto maybe_pd = AllocateProtocolDriver(pdo, &port);
  ASSERT_OK(maybe_pd.status());
  auto& pd = maybe_pd.value();
  pd->SetNumPeers(1);
  pd->SetHandler([](ServerRpcState* s) {
    ADD_FAILURE() << "should not get here";
    return std::function<void()>();
  });
}

TEST_P(ProtocolDriverTest, GetConnectionHandle) {
  ProtocolDriverOptions pdo = GetParam();
  int port = 0;
  auto maybe_pd = AllocateProtocolDriver(pdo, &port);
  ASSERT_OK(maybe_pd.status());
  auto& pd = maybe_pd.value();
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  pd->SetHandler([&](ServerRpcState* s) {
    ++server_rpc_count;
    return std::function<void()>();
  });
  std::string addr = pd->HandlePreConnect("", 0).value();
  ASSERT_EQ(server_rpc_count, 0);
}

TEST_P(ProtocolDriverTest, HandleConnect) {
  ProtocolDriverOptions pdo = GetParam();
  int port = 0;
  auto maybe_pd = AllocateProtocolDriver(pdo, &port);
  ASSERT_OK(maybe_pd.status());
  auto& pd = maybe_pd.value();
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  pd->SetHandler([&](ServerRpcState* s) {
    ++server_rpc_count;
    return std::function<void()>();
  });
  std::string addr = pd->HandlePreConnect("", 0).value();
  ASSERT_OK(pd->HandleConnect(addr, 0));
  ASSERT_EQ(server_rpc_count, 0);
}

TEST_P(ProtocolDriverTest, Invoke) {
  ProtocolDriverOptions pdo = GetParam();
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

  EXPECT_EQ(server_rpc_count, kNumIterations);
  EXPECT_EQ(client_rpc_count, kNumIterations);
}

TEST_P(ProtocolDriverTest, SelfEcho) {
  ProtocolDriverOptions pdo = GetParam();
  int port = 0;
  auto maybe_pd = AllocateProtocolDriver(pdo, &port);
  ASSERT_OK(maybe_pd.status());
  auto& pd = maybe_pd.value();
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  pd->SetHandler([&](ServerRpcState* s) {
    ++server_rpc_count;
    s->response.set_payload(s->request->payload());
    s->SendResponseIfSet();
    s->FreeStateIfSet();
    return std::function<void()>();
  });
  std::string addr = pd->HandlePreConnect("", 0).value();
  ASSERT_OK(pd->HandleConnect(addr, 0));

  std::atomic<int> client_rpc_count = 0;
  ClientRpcState rpc_state;
  rpc_state.request.set_payload("ping!");
  pd->InitiateRpc(0, &rpc_state, [&]() {
    ++client_rpc_count;
    EXPECT_EQ(rpc_state.request.payload(), rpc_state.response.payload());
    EXPECT_EQ(rpc_state.response.payload(), "ping!");
  });
  pd->ShutdownClient();
  EXPECT_EQ(server_rpc_count, 1);
  EXPECT_EQ(client_rpc_count, 1);
}

TEST_P(ProtocolDriverTest, Echo) {
  ProtocolDriverOptions pdo = GetParam();
  int port1 = 0;
  auto maybe_pd1 = AllocateProtocolDriver(pdo, &port1);
  ASSERT_OK(maybe_pd1.status());
  auto& pd1 = maybe_pd1.value();
  int port2 = 0;
  auto maybe_pd2 = AllocateProtocolDriver(pdo, &port2);
  ASSERT_OK(maybe_pd2.status());
  auto& pd2 = maybe_pd2.value();
  std::atomic<int> server_rpc_count = 0;
  pd2->SetNumPeers(1);
  pd2->SetHandler([&](ServerRpcState* s) {
    ++server_rpc_count;
    s->response.set_payload(s->request->payload());
    s->SendResponseIfSet();
    s->FreeStateIfSet();
    return std::function<void()>();
  });
  pd1->SetNumPeers(1);
  pd1->SetHandler([&](ServerRpcState* s) {
    ADD_FAILURE() << "should not get here";
    return std::function<void()>();
  });
  std::string addr1 = pd1->HandlePreConnect("", 0).value();
  std::string addr2 = pd2->HandlePreConnect("", 0).value();
  ASSERT_OK(pd1->HandleConnect(addr2, 0));
  ASSERT_OK(pd2->HandleConnect(addr1, 0));

  std::atomic<int> client_rpc_count = 0;
  ClientRpcState rpc_state;
  rpc_state.request.set_payload("ping!");
  pd1->InitiateRpc(0, &rpc_state, [&]() {
    ++client_rpc_count;
    EXPECT_EQ(rpc_state.request.payload(), rpc_state.response.payload());
    EXPECT_EQ(rpc_state.response.payload(), "ping!");
  });
  pd1->ShutdownClient();
  EXPECT_EQ(server_rpc_count, 1);
  EXPECT_EQ(client_rpc_count, 1);
}

void Echo(benchmark::State& state, ProtocolDriverOptions opts) {
  int port1 = 0;
  auto maybe_pd1 = AllocateProtocolDriver(opts, &port1);
  ASSERT_OK(maybe_pd1.status());
  auto& pd1 = maybe_pd1.value();
  int port2 = 0;
  auto maybe_pd2 = AllocateProtocolDriver(opts, &port2);
  ASSERT_OK(maybe_pd2.status());
  auto& pd2 = maybe_pd2.value();
  std::atomic<int> server_rpc_count = 0;
  pd2->SetNumPeers(1);
  pd2->SetHandler([&](ServerRpcState* s) {
    ++server_rpc_count;
    s->response.set_payload(s->request->payload());
    s->SendResponseIfSet();
    s->FreeStateIfSet();
    return std::function<void()>();
  });
  pd1->SetNumPeers(1);
  pd1->SetHandler([&](ServerRpcState* s) {
    ADD_FAILURE() << "should not get here";
    return std::function<void()>();
  });
  std::string addr1 = pd1->HandlePreConnect("", 0).value();
  std::string addr2 = pd2->HandlePreConnect("", 0).value();
  ASSERT_OK(pd1->HandleConnect(addr2, 0));
  ASSERT_OK(pd2->HandleConnect(addr1, 0));

  std::atomic<int> client_rpc_count = 0;
  ClientRpcState rpc_state;
  rpc_state.request.set_payload("ping!");
  for (auto s : state) {
    pd1->InitiateRpc(0, &rpc_state, [&]() {
      ++client_rpc_count;
      EXPECT_EQ(rpc_state.request.payload(), rpc_state.response.payload());
      EXPECT_EQ(rpc_state.response.payload(), "ping!");
    });
    pd1->ShutdownClient();
  }
}

ProtocolDriverOptions GrpcOptions() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  return pdo;
}

ProtocolDriverOptions GrpcAsynCallbackOptions() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc_async_callback");
  return pdo;
}

ProtocolDriverOptions DoubleBarrelGrpc() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("double_barrel");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "inline");
  AddServerStringOptionTo(pdo, "next_protocol_driver", "grpc");
  return pdo;
}

ProtocolDriverOptions GrpcPollingClientHandoffServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "handoff");
  return pdo;
}

ProtocolDriverOptions GrpcPollingClientPollingServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "polling");
  return pdo;
}

ProtocolDriverOptions GrpcCallbackClientInlineServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "callback");
  AddServerStringOptionTo(pdo, "server_type", "inline");
  return pdo;
}

ProtocolDriverOptions MercuryOptions() {
  ProtocolDriverOptions ret;
  ret.set_protocol_name("mercury");
  return ret;
}

void BM_GrpcEcho(benchmark::State& state) { Echo(state, GrpcOptions()); }

void BM_GrpcCallbackEcho(benchmark::State& state) {
  Echo(state, GrpcAsynCallbackOptions());
}

BENCHMARK(BM_GrpcEcho);
BENCHMARK(BM_GrpcCallbackEcho);

// clang-format off
INSTANTIATE_TEST_SUITE_P(ProtocolDriverTests, ProtocolDriverTest,
                         testing::Values(GrpcOptions(),
                                         GrpcAsynCallbackOptions(),
                                         GrpcPollingClientHandoffServer(),
                                         GrpcPollingClientPollingServer(),
                                         GrpcCallbackClientInlineServer(),
#ifdef WITH_MERCURY
                                         MercuryOptions(),
#endif
                                         DoubleBarrelGrpc()
                                         )
                         );
// clang-format on

}  // namespace distbench
