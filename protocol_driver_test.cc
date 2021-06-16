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

#include "distbench_utils.h"
#include "protocol_driver_allocator.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "benchmark/benchmark.h"
#include "glog/logging.h"

namespace distbench {

class ProtocolDriverTest
  : public testing::TestWithParam<ProtocolDriverOptions> {
};

TEST_P(ProtocolDriverTest, ctor) {
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(GetParam());
}

TEST_P(ProtocolDriverTest, initialize) {
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(GetParam());
  pd->SetNumPeers(1);
  ASSERT_OK(pd->Initialize("", AllocatePort()));
  pd->SetHandler([](ServerRpcState *s) {
    ADD_FAILURE() << "should not get here";
  });
}

TEST_P(ProtocolDriverTest, get_addr) {
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(GetParam());
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  ASSERT_OK(
      pd->Initialize("", AllocatePort()));
  pd->SetHandler([&](ServerRpcState *s) { ++server_rpc_count; });
  std::string addr = pd->HandlePreConnect("", 0).value();
  ASSERT_EQ(server_rpc_count, 0);
}

TEST_P(ProtocolDriverTest, get_set_addr) {
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(GetParam());
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  ASSERT_OK(pd->Initialize("", AllocatePort()));
  pd->SetHandler([&](ServerRpcState *s) { ++server_rpc_count; });
  std::string addr = pd->HandlePreConnect("", 0).value();
  ASSERT_OK(pd->HandleConnect(addr, 0));
  ASSERT_EQ(server_rpc_count, 0);
}

TEST_P(ProtocolDriverTest, invoke) {
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(GetParam());
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  ASSERT_OK(pd->Initialize("", AllocatePort()));
  pd->SetHandler([&](ServerRpcState *s) {
    ++server_rpc_count;
    s->send_response();
  });
  std::string addr = pd->HandlePreConnect("", 0).value();
  ASSERT_OK(pd->HandleConnect(addr, 0));

  std::atomic<int> client_rpc_count = 0;
  ClientRpcState rpc_state[10];
  for (int i = 0; i < 10; ++i) {
    pd->InitiateRpc(0, &rpc_state[i], [&]() { ++client_rpc_count; });
  }
  pd->ShutdownClient();
  EXPECT_EQ(server_rpc_count, 10);
  EXPECT_EQ(client_rpc_count, 10);
}

TEST_P(ProtocolDriverTest, self_echo) {
  std::unique_ptr<ProtocolDriver> pd = AllocateProtocolDriver(GetParam());
  pd->SetNumPeers(1);
  std::atomic<int> server_rpc_count = 0;
  ASSERT_OK(pd->Initialize("", AllocatePort()));
  pd->SetHandler([&](ServerRpcState *s) {
    ++server_rpc_count;
    s->response.set_payload(s->request->payload());
    s->send_response();
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

TEST_P(ProtocolDriverTest, echo) {
  std::unique_ptr<ProtocolDriver> pd1 = AllocateProtocolDriver(GetParam());
  std::unique_ptr<ProtocolDriver> pd2 = AllocateProtocolDriver(GetParam());
  std::atomic<int> server_rpc_count = 0;
  ASSERT_OK(pd2->Initialize("", AllocatePort()));
  pd2->SetNumPeers(1);
  pd2->SetHandler([&](ServerRpcState *s) {
    ++server_rpc_count;
    s->response.set_payload(s->request->payload());
    s->send_response();
  });
  ASSERT_OK(pd1->Initialize("", AllocatePort()));
  pd1->SetNumPeers(1);
  pd1->SetHandler([&](ServerRpcState *s) {
    ADD_FAILURE() << "should not get here";
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

void Echo(benchmark::State &state, ProtocolDriverOptions opts) {
  std::unique_ptr<ProtocolDriver> pd1 = AllocateProtocolDriver(opts);
  std::unique_ptr<ProtocolDriver> pd2 = AllocateProtocolDriver(opts);
  std::atomic<int> server_rpc_count = 0;
  ASSERT_OK(pd2->Initialize("", AllocatePort()));
  pd2->SetNumPeers(1);
  pd2->SetHandler([&](ServerRpcState *s) {
    ++server_rpc_count;
    s->response.set_payload(s->request->payload());
    s->send_response();
  });
  ASSERT_OK(pd1->Initialize("", AllocatePort()));
  pd1->SetNumPeers(1);
  pd1->SetHandler([&](ServerRpcState *s) {
    ADD_FAILURE() << "should not get here";
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
  ProtocolDriverOptions ret;
  ret.set_protocol_name("grpc");
  return ret;
}

ProtocolDriverOptions GrpcCallbackOptions() {
  ProtocolDriverOptions ret;
  ret.set_protocol_name("grpc_async_callback");
  return ret;
}

void BM_GrpcEcho(benchmark::State &state) {
  Echo(state, GrpcOptions());
}

void BM_GrpcCallbackEcho(benchmark::State &state) {
  Echo(state, GrpcCallbackOptions());
}

BENCHMARK(BM_GrpcEcho);
BENCHMARK(BM_GrpcCallbackEcho);

INSTANTIATE_TEST_SUITE_P(
    ProtocolDriverTests, ProtocolDriverTest,
    testing::Values(GrpcOptions(), GrpcCallbackOptions()));

}  // namespace distbench
