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
//#include "protocol_driver_allocator.h"
#include "absl/base/internal/sysinfo.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "google/protobuf/repeated_field.h"

namespace distbench {

absl::StatusOr<std::unique_ptr<ProtocolDriver>> AllocateProtocolDriver(
    ProtocolDriverOptions opts, int* port, int tree_depth = 0);

// ProtocolDriver ===============================================
ProtocolDriverDoubleBarrel::ProtocolDriverDoubleBarrel(int tree_depth) {
  tree_depth_ = tree_depth;
}

absl::Status ProtocolDriverDoubleBarrel::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {

  auto pdo = pd_opts;
  std::string subordinate_driver_type = "";
  google::protobuf::internal::RepeatedPtrIterator
      <distbench::NamedSetting> iter_subordinate;
  for (auto iter=pdo.server_settings().begin();
       iter!=pdo.server_settings().end();
       iter++) {
    if (iter->name()=="next_protocol_driver") {
      subordinate_driver_type = iter->string_value();
      break;
    }
  }
  pdo.set_protocol_name(subordinate_driver_type);

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

void ProtocolDriverDoubleBarrel::HandleConnectFailure(
    std::string_view local_connection_info) {
  barrel_1_->HandleConnectFailure(local_connection_info);
  barrel_2_->HandleConnectFailure(local_connection_info);
}

std::vector<TransportStat> ProtocolDriverDoubleBarrel::GetTransportStats() {
  std::vector<TransportStat> stats;
  return stats;
}

void ProtocolDriverDoubleBarrel::InitiateRpc(int peer_index, ClientRpcState* state,
                                     std::function<void(void)> done_callback) {
  if (use_barrel_1_) {
    barrel_1_->InitiateRpc(peer_index, state, done_callback);
  } else {
    barrel_2_->InitiateRpc(peer_index, state, done_callback);
  }
  use_barrel_1_ = !use_barrel_1_;
}

void ProtocolDriverDoubleBarrel::ChurnConnection(int peer) {
}

void ProtocolDriverDoubleBarrel::ShutdownClient() {
  barrel_1_->ShutdownClient();
  barrel_2_->ShutdownClient();
}

void ProtocolDriverDoubleBarrel::ShutdownServer() {
}

}  // namespace distbench
