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

#include "benchmark/benchmark.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "protocol_driver_allocator.h"

namespace distbench {

ProtocolDriverOptions PdoFromString(const std::string& s) {
  ProtocolDriverOptions pdo;
  ::google::protobuf::TextFormat::ParseFromString(s, &pdo);
  return pdo;
}

void ConsumeCpu() {
  int sum = 0;
  std::vector<int> rand_array;
  rand_array.resize(4096);
  std::srand(time(0));
  std::generate(rand_array.begin(), rand_array.end(), std::rand);
  std::sort(rand_array.begin(), rand_array.end());
  for (auto num : rand_array) benchmark::DoNotOptimize(sum += num);
}

void Echo(benchmark::State& state, std::string opts_string) {
  ProtocolDriverOptions opts = PdoFromString(opts_string);
  int port1 = 0;
  auto maybe_pd1 = AllocateProtocolDriver(opts, &port1);
  // ASSERT_OK(maybe_pd1.status());
  auto& pd1 = maybe_pd1.value();
  int port2 = 0;
  auto maybe_pd2 = AllocateProtocolDriver(opts, &port2);
  // ASSERT_OK(maybe_pd2.status());
  auto& pd2 = maybe_pd2.value();
  std::atomic<int> server_rpc_count = 0;
  pd2->SetNumPeers(1);
  pd2->SetHandler([&](ServerRpcState* s) {
    ++server_rpc_count;
    return [s]() {
      s->response.set_payload(s->request->payload());
      s->SendResponseIfSet();
      s->FreeStateIfSet();
    };
  });
  pd1->SetNumPeers(1);
  pd1->SetHandler([&](ServerRpcState* s) {
    // ADD_FAILURE() << "should not get here";
    s->SendResponseIfSet();
    s->FreeStateIfSet();
    return std::function<void()>();
  });
  std::string addr1 = pd1->HandlePreConnect("", 0).value();
  std::string addr2 = pd2->HandlePreConnect("", 0).value();
  if (!pd1->HandleConnect(addr2, 0).ok()) LOG(FATAL) << "HandleConnect failed";
  if (!pd2->HandleConnect(addr1, 0).ok()) LOG(FATAL) << "HandleConnect failed";

  std::atomic<int> client_rpc_count = 0;
  ClientRpcState rpc_state;
  rpc_state.request.set_payload("ping!");
  for (auto s : state) {
    client_rpc_count = 64;
    for (int i = 0; i < 64; ++i) {
      pd1->InitiateRpc(0, &rpc_state, [&]() {
        // EXPECT_EQ(rpc_state.request.payload(), rpc_state.response.payload());
        // EXPECT_EQ(rpc_state.response.payload(), "ping!");
        client_rpc_count--;
      });
    }
    while (client_rpc_count)
      ;
  }
  pd1->ShutdownClient();
}

std::string GrpcOptions() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  return pdo.DebugString();
}

std::string GrpcAsynCallbackOptions() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc_async_callback");
  return pdo.DebugString();
}

const int num_threads = 8;

std::string GrpcPollingClientHandoffElasticServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "handoff");
  AddServerStringOptionTo(pdo, "threadpool_type", "elastic");
  AddServerInt64OptionTo(pdo, "threadpool_size", num_threads);
  return pdo.DebugString();
}

std::string GrpcPollingClientHandoffSimpleServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "handoff");
  AddServerStringOptionTo(pdo, "threadpool_type", "simple");
  AddServerInt64OptionTo(pdo, "threadpool_size", num_threads);
  return pdo.DebugString();
}

std::string GrpcPollingClientHandoffCThreadServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "handoff");
  AddServerStringOptionTo(pdo, "threadpool_type", "cthread");
  return pdo.DebugString();
}

std::string GrpcPollingClientHandoffNullServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "handoff");
  AddServerStringOptionTo(pdo, "threadpool_type", "null");
  AddServerInt64OptionTo(pdo, "threadpool_size", num_threads);
  return pdo.DebugString();
}

std::string GrpcPollingClientHandoffMercuryServer() {
  ProtocolDriverOptions pdo;
  pdo.set_protocol_name("grpc");
  AddClientStringOptionTo(pdo, "client_type", "polling");
  AddServerStringOptionTo(pdo, "server_type", "handoff");
  AddServerStringOptionTo(pdo, "threadpool_type", "mercury");
  AddServerInt64OptionTo(pdo, "threadpool_size", num_threads);
  return pdo.DebugString();
}

void BM_GrpcEcho(benchmark::State& state) { Echo(state, GrpcOptions()); }

void BM_GrpcCallbackEcho(benchmark::State& state) {
  Echo(state, GrpcAsynCallbackOptions());
}

void BM_GrpcHandoffEchoElastic(benchmark::State& state) {
  Echo(state, GrpcPollingClientHandoffElasticServer());
}

void BM_GrpcHandoffEchoSimple(benchmark::State& state) {
  Echo(state, GrpcPollingClientHandoffSimpleServer());
}

void BM_GrpcHandoffEchoMercury(benchmark::State& state) {
  Echo(state, GrpcPollingClientHandoffMercuryServer());
}

void BM_GrpcHandoffEchoCThread(benchmark::State& state) {
  Echo(state, GrpcPollingClientHandoffCThreadServer());
}

void BM_GrpcHandoffEchoNull(benchmark::State& state) {
  Echo(state, GrpcPollingClientHandoffNullServer());
}

BENCHMARK(BM_GrpcEcho);
BENCHMARK(BM_GrpcCallbackEcho);
BENCHMARK(BM_GrpcHandoffEchoNull);
BENCHMARK(BM_GrpcHandoffEchoElastic);
BENCHMARK(BM_GrpcHandoffEchoSimple);
#ifdef WITH_MERCURY
BENCHMARK(BM_GrpcHandoffEchoMercury);
#endif
BENCHMARK(BM_GrpcHandoffEchoCThread);

}  // namespace distbench
