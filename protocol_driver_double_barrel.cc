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

#include "protocol_driver_double_barrel.h"

#include "protocol_driver_allocator.h"
#include "absl/base/internal/sysinfo.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "google/protobuf/repeated_field.h"

namespace distbench {

// ProtocolDriver ===============================================
ProtocolDriverDoubleBarrel::ProtocolDriverDoubleBarrel(int tree_depth) {
  tree_depth_ = tree_depth;
}

typedef google::protobuf::RepeatedPtrField<NamedSetting> ServerSetting;

absl::Status ProtocolDriverDoubleBarrel::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {

  auto pdo = pd_opts;
  auto server_settings = pdo.mutable_server_settings();
  ServerSetting::iterator next_protocol_driver_it;
  for (auto it=server_settings->begin(); it!=server_settings->end(); it++) {
      if (it->name()=="next_protocol_driver") {
        next_protocol_driver_it = it;
        pdo.set_protocol_name(it->string_value());
      }
  }
  server_settings->erase(next_protocol_driver_it);

  auto maybe_barrel_1 = AllocateProtocolDriver(pdo, &port_1_, tree_depth_+1);
  if (!maybe_barrel_1.ok()) return maybe_barrel_1.status();
  barrel_1_ = std::move(maybe_barrel_1.value());

  auto maybe_barrel_2 = AllocateProtocolDriver(pdo, &port_2_, tree_depth_+1);
  if (!maybe_barrel_2.ok()) return maybe_barrel_2.status();
  barrel_2_ = std::move(maybe_barrel_2.value());

  return absl::OkStatus();
}

absl::Status ProtocolDriverDoubleBarrel::InitializeClient(
    const ProtocolDriverOptions& pd_opts) {
  return absl::UnimplementedError("InitializeClient is not implemented for double_barrel.");
}

absl::Status ProtocolDriverDoubleBarrel::InitializeServer(
    const ProtocolDriverOptions& pd_opts, int* port) {
  return absl::UnimplementedError("InitializeServer is not implemented for double_barrel.");
}

void ProtocolDriverDoubleBarrel::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  barrel_1_->SetHandler(handler);
  barrel_2_->SetHandler(handler);
}

void ProtocolDriverDoubleBarrel::SetNumPeers(int num_peers) {
  barrel_1_->SetNumPeers(num_peers);
  barrel_2_->SetNumPeers(num_peers);
}

ProtocolDriverDoubleBarrel::~ProtocolDriverDoubleBarrel() {
}

absl::StatusOr<std::string> ProtocolDriverDoubleBarrel::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  return barrel_1_->HandlePreConnect(remote_connection_info, peer);
  // barrell_2_'s server is not used.
}

absl::Status ProtocolDriverDoubleBarrel::HandleConnect(
    std::string remote_connection_info, int peer) {
  auto ret = barrel_1_->HandleConnect(remote_connection_info, peer);
  if (!ret.ok()) return ret;
  return barrel_2_->HandleConnect(remote_connection_info, peer);
}

#define APPEND_VECTOR(a, b) \
    (a.insert(a.end(), b.begin(), b.end()))

void ProtocolDriverDoubleBarrel::HandleConnectFailure(
    std::string_view local_connection_info) {
  barrel_1_->HandleConnectFailure(local_connection_info);
  barrel_2_->HandleConnectFailure(local_connection_info);
}

std::vector<TransportStat> ProtocolDriverDoubleBarrel::GetTransportStats() {
  std::vector<TransportStat> transport_stats;

  transport_stats.push_back({"barrel_1_server_stats", {0}});
  auto barrel_1_server_stats = ((ProtocolDriverServer*)
                                barrel_1_.get())->GetTransportStats();
  APPEND_VECTOR(transport_stats, barrel_1_server_stats);

  transport_stats.push_back({"barrel_1_client_stats", {0}});
  auto barrel_1_client_stats = ((ProtocolDriverClient*)
                                barrel_1_.get())->GetTransportStats();
  APPEND_VECTOR(transport_stats, barrel_1_client_stats);

  transport_stats.push_back({"barrel_2_server_stats", {0}});
  auto barrel_2_server_stats = ((ProtocolDriverServer*)
                                barrel_2_.get())->GetTransportStats();
  APPEND_VECTOR(transport_stats, barrel_2_server_stats);

  transport_stats.push_back({"barrel_2_client_stats", {0}});
  auto barrel_2_client_stats = ((ProtocolDriverClient*)
                                barrel_2_.get())->GetTransportStats();
  APPEND_VECTOR(transport_stats, barrel_2_client_stats);

  return transport_stats;
}

void ProtocolDriverDoubleBarrel::InitiateRpc(int peer_index, ClientRpcState* state,
                                     std::function<void(void)> done_callback) {
  if (std::atomic_fetch_xor_explicit(&use_barrel_1_, 1, std::memory_order_relaxed)) {
    barrel_1_->InitiateRpc(peer_index, state, done_callback);
  } else {
    barrel_2_->InitiateRpc(peer_index, state, done_callback);
  }
}

void ProtocolDriverDoubleBarrel::ChurnConnection(int peer) {
  barrel_1_->ChurnConnection(peer);
  barrel_2_->ChurnConnection(peer);
}

void ProtocolDriverDoubleBarrel::ShutdownClient() {
  barrel_1_->ShutdownClient();
  barrel_2_->ShutdownClient();
}

void ProtocolDriverDoubleBarrel::ShutdownServer() {
  barrel_1_->ShutdownServer();
  barrel_2_->ShutdownServer();
}

}  // namespace distbench
