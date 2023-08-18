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

#include "protocol_driver_double_barrel.h"

#include "absl/base/internal/sysinfo.h"
#include "absl/log/log.h"
#include "distbench_utils.h"
#include "google/protobuf/repeated_field.h"
#include "protocol_driver_allocator.h"

namespace distbench {

// ProtocolDriver ===============================================
ProtocolDriverDoubleBarrel::ProtocolDriverDoubleBarrel(int tree_depth) {
  tree_depth_ = tree_depth;
}

absl::Status ProtocolDriverDoubleBarrel::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  auto pdo = pd_opts;
  auto server_settings = pdo.mutable_server_settings();
  ::google::protobuf::RepeatedPtrField<NamedSetting>::iterator
      next_protocol_driver_it = server_settings->end();
  for (auto it = server_settings->begin(); it != server_settings->end(); it++) {
    if (it->name() == "next_protocol_driver") {
      next_protocol_driver_it = it;
      pdo.set_protocol_name(it->string_value());
    }
  }
  if (next_protocol_driver_it != server_settings->end())
    server_settings->erase(next_protocol_driver_it);

  auto maybe_instance_1 =
      AllocateProtocolDriver(pdo, &port_1_, tree_depth_ + 1);
  if (!maybe_instance_1.ok()) return maybe_instance_1.status();
  instance_1_ = std::move(maybe_instance_1.value());

  auto maybe_instance_2 =
      AllocateProtocolDriver(pdo, &port_2_, tree_depth_ + 1);
  if (!maybe_instance_2.ok()) return maybe_instance_2.status();
  instance_2_ = std::move(maybe_instance_2.value());

  return absl::OkStatus();
}

void ProtocolDriverDoubleBarrel::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  instance_1_->SetHandler(handler);
  instance_2_->SetHandler(handler);
}

void ProtocolDriverDoubleBarrel::SetNumPeers(int num_peers) {
  instance_1_->SetNumPeers(num_peers);
  instance_2_->SetNumPeers(num_peers);
}

ProtocolDriverDoubleBarrel::~ProtocolDriverDoubleBarrel() {}

absl::StatusOr<std::string> ProtocolDriverDoubleBarrel::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  return instance_1_->HandlePreConnect(remote_connection_info, peer);
  // instance_2_'s server is not used.
}

absl::Status ProtocolDriverDoubleBarrel::HandleConnect(
    std::string remote_connection_info, int peer) {
  auto ret = instance_1_->HandleConnect(remote_connection_info, peer);
  if (!ret.ok()) return ret;
  return instance_2_->HandleConnect(remote_connection_info, peer);
}

void ProtocolDriverDoubleBarrel::HandleConnectFailure(
    std::string_view local_connection_info) {
  instance_1_->HandleConnectFailure(local_connection_info);
  instance_2_->HandleConnectFailure(local_connection_info);
}

std::vector<TransportStat> ProtocolDriverDoubleBarrel::GetTransportStats() {
  std::vector<TransportStat> transport_stats;

  std::string prefix = "";
  auto add_prefix = [&](TransportStat& ts) { ts.name.insert(0, prefix); };

  prefix = "instance_1/";
  auto instance_1_stats = instance_1_->GetTransportStats();
  std::for_each(instance_1_stats.begin(), instance_1_stats.end(), add_prefix);
  transport_stats.insert(transport_stats.end(), instance_1_stats.begin(),
                         instance_1_stats.end());

  prefix = "instance_2/";
  auto instance_2_stats = instance_2_->GetTransportStats();
  std::for_each(instance_2_stats.begin(), instance_2_stats.end(), add_prefix);
  transport_stats.insert(transport_stats.end(), instance_2_stats.begin(),
                         instance_2_stats.end());

  return transport_stats;
}

void ProtocolDriverDoubleBarrel::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  if (0x1 & std::atomic_fetch_add_explicit(&use_instance_1_, 1,
                                           std::memory_order_relaxed)) {
    instance_1_->InitiateRpc(peer_index, state, done_callback);
  } else {
    instance_2_->InitiateRpc(peer_index, state, done_callback);
  }
}

void ProtocolDriverDoubleBarrel::ChurnConnection(int peer) {
  instance_1_->ChurnConnection(peer);
  instance_2_->ChurnConnection(peer);
}

void ProtocolDriverDoubleBarrel::ShutdownClient() {
  instance_1_->ShutdownClient();
  instance_2_->ShutdownClient();
}

void ProtocolDriverDoubleBarrel::ShutdownServer() {
  instance_1_->ShutdownServer();
  instance_2_->ShutdownServer();
}

}  // namespace distbench
