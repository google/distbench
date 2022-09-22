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

#include "protocol_driver_mercury.h"

#include <mercury.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>

#include "absl/base/const_init.h"
#include "absl/strings/str_replace.h"
#include "absl/synchronization/mutex.h"
#include "distbench_utils.h"
#include "glog/logging.h"

namespace {
ABSL_CONST_INIT absl::Mutex mercury_init_mutex(absl::kConstInit);
}  // namespace

namespace distbench {

ProtocolDriverMercury::ProtocolDriverMercury() {}

absl::Status ProtocolDriverMercury::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  std::string netdev_name = pd_opts.netdev_name();
  auto maybe_ip = IpAddressForDevice(netdev_name, 4);
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  server_socket_address_ = SocketAddressForIp(server_ip_address_, *port);

  // TODO: Choosen interface does not seem respected
  std::string info_string =
      GetNamedSettingString(pd_opts.server_settings(), "hg_init_info_string",
                            "ofi+tcp://__SERVER_IP__");
  info_string = absl::StrReplaceAll(
      info_string, {{"__SERVER_IP__", server_socket_address_}});
  {
    absl::MutexLock l(&mercury_init_mutex);
    hg_class_ = HG_Init(info_string.c_str(), /*listen=*/true);
  }
  if (hg_class_ == nullptr) {
    return absl::UnknownError("HG_Init: failed");
  }

  hg_context_ = HG_Context_create(hg_class_);
  if (hg_context_ == nullptr) {
    return absl::UnknownError("HG_Context_create: failed");
  }

  mercury_generic_rpc_id_ = HG_Register_name(
      hg_class_, "mercury_generic_rpc", StaticRpcServerSerialize,
      StaticRpcServerSerialize, StaticRpcServerCallback);

  hg_return_t hg_ret;
  hg_ret = HG_Register_data(hg_class_, mercury_generic_rpc_id_, /*data=*/this,
                            /*free_callback=*/NULL);
  if (hg_ret != HG_SUCCESS) {
    return absl::UnknownError("HG_Register_data: failed");
  }

  hg_addr_t addr;
  char buf[256] = {'\0'};
  hg_size_t buf_size = 256;

  hg_ret = HG_Addr_self(hg_class_, &addr);
  if (hg_ret != HG_SUCCESS) {
    return absl::UnknownError("HG_Addr_self: failed");
  }

  hg_ret = HG_Addr_to_string(hg_class_, buf, &buf_size, addr);
  if (hg_ret != HG_SUCCESS) {
    return absl::UnknownError("HG_Addr_to_string: failed");
  }

  hg_ret = HG_Addr_free(hg_class_, addr);
  if (hg_ret != HG_SUCCESS) {
    return absl::UnknownError("HG_Addr_free: failed");
  }

  server_socket_address_ = std::string(buf);

  PrintMercuryVersion();
  VLOG(1) << "Mercury Traffic server listening on " << server_socket_address_;
  progress_thread_ = RunRegisteredThread(
      "MercuryProgress", [=]() { this->RpcCompletionThread(); });
  return absl::OkStatus();
}

void ProtocolDriverMercury::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  handler_ = handler;
}

void ProtocolDriverMercury::SetNumPeers(int num_peers) {
  remote_addresses_.resize(num_peers);
}

ProtocolDriverMercury::~ProtocolDriverMercury() {
  ShutdownServer();
  ShutdownClient();
  {
    absl::MutexLock l(&mercury_init_mutex);
    if (hg_class_ != nullptr) {
      HG_Deregister(hg_class_, mercury_generic_rpc_id_);
    }
    if (hg_context_ != nullptr) {
      HG_Context_destroy(hg_context_);
    }
    if (hg_class_ != nullptr) {
      HG_Finalize(hg_class_);
    }
  }
}

absl::StatusOr<std::string> ProtocolDriverMercury::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  return server_socket_address_;
}

absl::Status ProtocolDriverMercury::HandleConnect(
    std::string remote_connection_info, int peer) {
  CHECK_GE(peer, 0);
  CHECK_LT((size_t)peer, remote_addresses_.size());

  VLOG(1) << "Connect to peer " << peer << " at " << remote_connection_info;

  hg_return_t hg_ret;
  hg_ret = HG_Addr_lookup2(hg_class_, remote_connection_info.c_str(),
                           &remote_addresses_[peer]);
  if (hg_ret != HG_SUCCESS) {
    return absl::UnknownError("HG_Addr_lookup: failed");
  }

  return absl::OkStatus();
}

std::vector<TransportStat> ProtocolDriverMercury::GetTransportStats() {
  return {};
}

void ProtocolDriverMercury::PrintMercuryVersion() {
  unsigned major;
  unsigned minor;
  unsigned patch;
  absl::MutexLock l(&mercury_init_mutex);

  static bool already_done = false;
  if (already_done) return;
  already_done = true;
  HG_Version_get(&major, &minor, &patch);
  VLOG(1) << "Mercury version " << major << "." << minor << "." << patch;
}

namespace {
struct PendingRpc {
  GenericRequest request;
  GenericResponse response;
  std::function<void(void)> done_callback;
  ClientRpcState* state;
  ProtocolDriverMercury* this_pd;
  mercury_generic_rpc_string_t encoded_request;
  hg_handle_t hg_handle;
};
}  // anonymous namespace

void ProtocolDriverMercury::InitiateRpc(
    int peer_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  CHECK_GE(peer_index, 0);
  CHECK_LT((size_t)peer_index, remote_addresses_.size());

  ++pending_rpcs_;

  hg_handle_t target_handle;
  hg_return_t hg_ret;
  hg_ret = HG_Create(hg_context_, remote_addresses_[peer_index],
                     mercury_generic_rpc_id_, &target_handle);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "HG_Create: failed";
    return;
  }

  PendingRpc* new_rpc = new PendingRpc();
  new_rpc->done_callback = done_callback;
  new_rpc->state = state;
  new_rpc->request = std::move(state->request);
  new_rpc->this_pd = this;
  new_rpc->hg_handle = target_handle;

  new_rpc->request.SerializeToString(&new_rpc->encoded_request.string);

  hg_ret = HG_Forward(target_handle, StaticClientCallback, new_rpc,
                      &new_rpc->encoded_request);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << " HG_Forward: failed";
  }
}

void ProtocolDriverMercury::ChurnConnection(int peer) {}

void ProtocolDriverMercury::ShutdownServer() {}

void ProtocolDriverMercury::ShutdownClient() {
  while (pending_rpcs_) {
    sched_yield();
  }
  if (shutdown_.TryToNotify()) {
    if (progress_thread_.joinable()) {
      progress_thread_.join();
    }
  }
  for (hg_addr_t addr : remote_addresses_) {
    HG_Addr_free(hg_class_, addr);
  }
  remote_addresses_.resize(0);
}

void ProtocolDriverMercury::RpcCompletionThread() {
  while (!shutdown_.HasBeenNotified()) {
    hg_return_t hg_ret;
    unsigned int actual_count = 0;

    // Process callbacks based on the network event received
    hg_ret = HG_Trigger(hg_context_, /*timeout=*/0, /*max_count=1*/ 1,
                        &actual_count);
    if (hg_ret != HG_SUCCESS && hg_ret != HG_TIMEOUT) {
      LOG(ERROR) << "HG_Trigger: failed with " << hg_ret
                 << " actual_count:" << actual_count;
    }

    // Process network events
    hg_ret = HG_Progress(hg_context_, /*timeout_ms=*/1);
    if (hg_ret != HG_SUCCESS && hg_ret != HG_TIMEOUT) {
      LOG(ERROR) << "HG_Progress: failed with " << hg_ret
                 << " actual_count:" << actual_count;
    }
  }
}

hg_return_t ProtocolDriverMercury::StaticClientCallback(
    const struct hg_cb_info* callback_info) {
  struct PendingRpc* rpc = (struct PendingRpc*)callback_info->arg;
  return rpc->this_pd->RpcClientCallback(callback_info);
}

// This function performs both serialization and deserialization (specified
// by proc).
hg_return_t ProtocolDriverMercury::StaticRpcServerSerialize(hg_proc_t proc,
                                                            void* data) {
  hg_return_t hg_ret;
  mercury_generic_rpc_string_t* struct_data =
      (mercury_generic_rpc_string_t*)data;
  size_t slen = struct_data->string.size();
  hg_ret = hg_proc_hg_int64_t(proc, &slen);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "Serialization error (size)";
  }
  hg_ret = hg_proc_hg_int64_t(proc, &slen);
  struct_data->string.resize(slen);
  hg_ret = hg_proc_bytes(proc, &struct_data->string[0], slen);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "Serialization error (data)";
  }
  return hg_ret;
}

hg_return_t ProtocolDriverMercury::StaticRpcServerCallback(hg_handle_t handle) {
  const struct hg_info* info = HG_Get_info(handle);
  ProtocolDriverMercury* this_pd =
      (ProtocolDriverMercury*)HG_Registered_data(info->hg_class, info->id);
  return this_pd->RpcServerCallback(handle);
}

hg_return_t ProtocolDriverMercury::RpcServerCallback(hg_handle_t handle) {
  mercury_generic_rpc_string_t input;
  hg_return_t hg_ret;

  hg_ret = HG_Get_input(handle, &input);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "HG_Get_input: failed";
  }

  ServerRpcState* rpc_state = new ServerRpcState();
  rpc_state->have_dedicated_thread = false;

  distbench::GenericRequest* request = new distbench::GenericRequest();
  bool success = request->ParseFromString(input.string);
  if (!success) {
    LOG(ERROR) << "Unable to decode payload !";
  }
  hg_ret = HG_Free_input(handle, &input);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "HG_Free_input: failed";
  }
  hg_ret = HG_Destroy(handle);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "HG_Destroy: failed";
  }

  rpc_state->request = request;
  rpc_state->SetSendResponseFunction([rpc_state, handle]() {
    mercury_generic_rpc_string_t* result = new mercury_generic_rpc_string_t();
    rpc_state->response.SerializeToString(&result->string);
    hg_return_t hg_ret;
    hg_ret =
        HG_Respond(handle, StaticRpcServerDoneCallback, /*arg=*/result, result);
    if (hg_ret != HG_SUCCESS) {
      LOG(ERROR) << "HG_Respond: failed";
    }
  });
  rpc_state->SetFreeStateFunction([rpc_state]() {
    delete rpc_state->request;
    delete rpc_state;
  });

  auto fct_action_list_thread = handler_(rpc_state);
  if (fct_action_list_thread) {
    RunRegisteredThread("DedicatedActionListThread", fct_action_list_thread)
        .detach();
  }
  return HG_SUCCESS;
}

hg_return_t ProtocolDriverMercury::StaticRpcServerDoneCallback(
    const struct hg_cb_info* hg_cb_info) {
  if (hg_cb_info->ret != HG_SUCCESS) {
    LOG(ERROR) << "StaticRpcServerDoneCallback with non success ret="
               << hg_cb_info->ret;
  }
  mercury_generic_rpc_string_t* result =
      (mercury_generic_rpc_string_t*)hg_cb_info->arg;
  delete result;
  return HG_SUCCESS;
}

hg_return_t ProtocolDriverMercury::RpcClientCallback(
    const struct hg_cb_info* callback_info) {
  struct PendingRpc* rpc = (struct PendingRpc*)callback_info->arg;
  rpc->state->success = (callback_info->ret == 0);
  mercury_generic_rpc_string_t result;
  hg_return_t hg_ret = HG_Get_output(rpc->hg_handle, &result);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "HG_Get_output: failed";
    return hg_ret;
  }

  bool success = rpc->response.ParseFromString(result.string);
  if (!success) {
    LOG(ERROR) << "Unable to decode payload";
  } else {
    rpc->state->request = std::move(rpc->request);
    rpc->state->response = std::move(rpc->response);
  }
  rpc->state->success = success;
  rpc->done_callback();

  hg_ret = HG_Free_output(rpc->hg_handle, &result);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "HG_Free_output: failed";
  }

  hg_ret = HG_Destroy(rpc->hg_handle);
  if (hg_ret != HG_SUCCESS) {
    LOG(ERROR) << "HG_Destroy: failed";
  }

  delete rpc;
  --pending_rpcs_;
  return HG_SUCCESS;
}

}  // namespace distbench
