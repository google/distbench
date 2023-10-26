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

#include "composable_rpc_counter.h"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/internal/sysinfo.h"
#include "absl/log/log.h"
#include "distbench_utils.h"
#include "google/protobuf/repeated_field.h"
#include "protocol_driver_allocator.h"

namespace distbench {

// ProtocolDriver ===============================================
ComposableRpcCounter::ComposableRpcCounter(int tree_depth) {
  tree_depth_ = tree_depth;
}

absl::Status ComposableRpcCounter::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  auto pdo = pd_opts;
  auto server_settings = pdo.mutable_server_settings();
  ::google::protobuf::RepeatedPtrField<NamedSetting>::iterator
      next_protocol_driver_it = server_settings->end();
  for (auto it = server_settings->begin(); it != server_settings->end(); it++) {
    if (it->name() == "driver_under_test") {
      next_protocol_driver_it = it;
      pdo.set_protocol_name(it->string_value());
    }
  }
  if (next_protocol_driver_it != server_settings->end())
    server_settings->erase(next_protocol_driver_it);

  auto maybe_pd_instance_ = AllocateProtocolDriver(pdo, port, tree_depth_ + 1);
  if (!maybe_pd_instance_.ok()) return maybe_pd_instance_.status();
  pd_instance_ = std::move(maybe_pd_instance_.value());

  return absl::OkStatus();
}

void ComposableRpcCounter::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  auto server_handler = [this, handler](ServerRpcState* state) {
    std::atomic_fetch_add_explicit(&server_rpc_cnt_, 1,
                                   std::memory_order_relaxed);
    return handler(state);
  };
  pd_instance_->SetHandler(server_handler);
}

void ComposableRpcCounter::SetNumPeers(int num_peers) {
  pd_instance_->SetNumPeers(num_peers);
}

ComposableRpcCounter::~ComposableRpcCounter() {}

absl::StatusOr<std::string> ComposableRpcCounter::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  return pd_instance_->HandlePreConnect(remote_connection_info, peer);
}

absl::Status ComposableRpcCounter::HandleConnect(
    std::string remote_connection_info, int peer) {
  return pd_instance_->HandleConnect(remote_connection_info, peer);
}

void ComposableRpcCounter::HandleConnectFailure(
    std::string_view local_connection_info) {
  pd_instance_->HandleConnectFailure(local_connection_info);
}

std::vector<TransportStat> ComposableRpcCounter::GetTransportStats() {
  std::vector<TransportStat> transport_stats =
      pd_instance_->GetTransportStats();
  transport_stats.push_back({"client_rpc_cnt", client_rpc_cnt_});
  transport_stats.push_back({"server_rpc_cnt", server_rpc_cnt_});
  return transport_stats;
}

void ComposableRpcCounter::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  pd_instance_->InitiateRpc(peer_index, state, done_callback);
  std::atomic_fetch_add_explicit(&client_rpc_cnt_, 1,
                                 std::memory_order_relaxed);
}

void ComposableRpcCounter::ChurnConnection(int peer) {
  pd_instance_->ChurnConnection(peer);
}

void ComposableRpcCounter::ShutdownClient() { pd_instance_->ShutdownClient(); }

void ComposableRpcCounter::ShutdownServer() { pd_instance_->ShutdownServer(); }

}  // namespace distbench
