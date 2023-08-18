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

#include "protocol_driver_homa.h"

#include <arpa/inet.h>
#include <sys/mman.h>

#include "absl/base/internal/sysinfo.h"
#include "distbench_payload.h"
#include "external/homa_module/homa.h"
#include "glog/logging.h"

namespace distbench {

namespace {

// This is used instead of a zero-byte payload. No protobuf can
// serialize to this value, so it is unambiguous.
const char empty_message_placeholder[] = "\7";

std::string_view PeekAtMessage(homa::receiver* r) {
  return {r->get<char>(0), r->contiguous(0)};
}

bool SlowParse(homa::receiver* r, size_t msg_length,
               GenericRequestResponse* out) {
  char rx_buf[1048576];
  r->copy_out((void*)rx_buf, 0, sizeof(rx_buf));
  if (msg_length == 1 && rx_buf[0] == empty_message_placeholder[0]) {
    msg_length = 0;
  }
  return out->ParseFromArray(rx_buf, msg_length);
}

bool FastParse(homa::receiver* r, size_t msg_length,
               GenericRequestResponse* out) {
  // Try the fast way:
  std::string_view initial_chunk = PeekAtMessage(r);
  if (initial_chunk.length() == 1 &&
      initial_chunk[0] == empty_message_placeholder[0]) {
    return true;
  }

  if (msg_length <= 4096 && initial_chunk.length() == msg_length) {
    return out->ParseFromArray(initial_chunk.data(), msg_length);
  }

  size_t offset = 0;
  size_t metadata_length = MetaDataLength(initial_chunk, msg_length, &offset);
  if (metadata_length <= initial_chunk.length()) {
    bool ret = out->ParseFromArray(initial_chunk.data(), metadata_length);
    absl::Cord payload;

    for (int i = 0; i < HOMA_MAX_BPAGES; ++i) {
      size_t chunk_length = r->contiguous(offset);
      absl::string_view s(r->get<char>(offset), chunk_length);
      offset += chunk_length;
      payload.Append(absl::MakeCordFromExternal(s, []() {}));
      if (offset == msg_length) {
        out->set_payload(std::move(payload));
        break;
      }
    }
    return ret;
  }

  // Fall back to the slow way (should not be possible):
  return SlowParse(r, msg_length, out);
}

bool Parse(homa::receiver* r, size_t msg_length, GenericRequestResponse* out,
           bool avoid_copy) {
  if (avoid_copy) {
    return FastParse(r, msg_length, out);
  } else {
    return SlowParse(r, msg_length, out);
  }
}

}  // namespace

///////////////////////////////////
// ProtocolDriverHoma Methods //
///////////////////////////////////

ProtocolDriverHoma::ProtocolDriverHoma() {}

absl::Status ProtocolDriverHoma::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  std::set<std::string> known_settings = {"threadpool_type", "threadpool_size",
                                          "ping_pong",       "nocopy",
                                          "client_threads",  "server_threads"};

  for (const auto& setting : pd_opts.server_settings()) {
    if (known_settings.find(setting.name()) == known_settings.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown protocol driver option: ", setting.name()));
    }
  }

  auto threadpool_size = GetNamedServerSettingInt64(
      pd_opts, "threadpool_size", absl::base_internal::NumCPUs());
  auto threadpool_type =
      GetNamedServerSettingString(pd_opts, "threadpool_type", "elastic");

  ping_pong_ = GetNamedServerSettingInt64(pd_opts, "ping_pong", false);
  nocopy_ = GetNamedServerSettingInt64(pd_opts, "nocopy", true);
  int client_threads = GetNamedServerSettingInt64(pd_opts, "client_threads", 2);
  int server_threads = GetNamedServerSettingInt64(pd_opts, "server_threads", 3);

  auto tp = CreateThreadpool(threadpool_type, threadpool_size);
  if (!tp.ok()) {
    return tp.status();
  }
  actionlist_thread_pool_ = std::move(tp.value());

  if (pd_opts.has_netdev_name()) {
    netdev_name_ = pd_opts.netdev_name();
  }

  auto maybe_ip = IpAddressForDevice(netdev_name_, pd_opts.ip_version());
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  int af = server_ip_address_.Family();

  homa_client_sock_ = socket(af, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_HOMA);
  if (homa_client_sock_ < 0) {
    return absl::UnknownError(
        absl::StrCat(strerror(errno), " creating client homa socket"));
  }
  client_buffer_ = mmap(NULL, kHomaBufferSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  struct homa_set_buf_args arg;
  arg.start = client_buffer_;
  arg.length = kHomaBufferSize;
  setsockopt(homa_client_sock_, IPPROTO_HOMA, SO_HOMA_SET_BUF, &arg,
             sizeof(arg));
  homa_server_sock_ = socket(af, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_HOMA);
  if (homa_server_sock_ < 0) {
    return absl::UnknownError(
        absl::StrCat(strerror(errno), " creating server homa socket"));
  }
  server_buffer_ = mmap(NULL, kHomaBufferSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  arg.start = server_buffer_;
  arg.length = kHomaBufferSize;
  setsockopt(homa_server_sock_, IPPROTO_HOMA, SO_HOMA_SET_BUF, &arg,
             sizeof(arg));

  // Must be started after setting up server_buffer_, client_buffer_
  client_completion_threads_.reserve(client_threads);
  server_threads_.reserve(server_threads);
  for (int i = 0; i < client_threads; ++i) {
    client_completion_threads_.push_back(RunRegisteredThread(
        "HomaClient", [this, i]() { this->ClientCompletionThread(i); }));
  }
  for (int i = 0; i < client_threads; ++i) {
    server_threads_.push_back(
        RunRegisteredThread("HomaServer", [this]() { this->ServerThread(); }));
  }

  sockaddr_in_union bind_addr = {};
  int bind_err = 0;
  if (af == AF_INET) {
    bind_addr.in4.sin_family = AF_INET;
    bind_addr.in4.sin_port = htons(*port);
    bind_addr.in4.sin_addr.s_addr = INADDR_ANY;
    bind_err = bind(homa_server_sock_, &bind_addr.sa, sizeof(bind_addr.in4));
  } else {
    bind_addr.in6.sin6_family = AF_INET6;
    bind_addr.in6.sin6_port = htons(*port);
    bind_addr.in6.sin6_addr = in6addr_any;
    bind_err = bind(homa_server_sock_, &bind_addr.sa, sizeof(bind_addr.in6));
  }

  if (bind_err) {
    return absl::UnknownError(absl::StrCat(strerror(errno), " family:", af,
                                           " binding server socket to port ",
                                           *port));
  }
  sockaddr_in_union server_sock_addr = {};
  socklen_t len = sizeof(server_sock_addr);
  if (getsockname(homa_server_sock_, &server_sock_addr.sa, &len) < 0) {
    return absl::UnknownError(
        absl::StrCat(strerror(errno), " getting sockname from server socket"));
  }

  if (server_sock_addr.in4.sin_family == AF_INET) {
    *port = ntohs(server_sock_addr.in4.sin_port);
    my_server_socket_address_ =
        absl::StrCat(server_ip_address_.ip(), ":", *port);
  } else if (server_sock_addr.in6.sin6_family == AF_INET6) {
    *port = ntohs(server_sock_addr.in6.sin6_port);
    my_server_socket_address_ =
        absl::StrCat("[", server_ip_address_.ip(), "]:", *port);
  } else {
    return absl::UnknownError(
        absl::StrCat("Unknown address family for homa socket: ",
                     server_sock_addr.in4.sin_family));
  }
  server_port_ = *port;

  return absl::OkStatus();
}

ProtocolDriverHoma::~ProtocolDriverHoma() {
  ShutdownServer();
  ShutdownClient();
}

void ProtocolDriverHoma::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  rpc_handler_ = handler;
  handler_set_.TryToNotify();
}

void ProtocolDriverHoma::SetNumPeers(int num_peers) {
  peer_addresses_.resize(num_peers);
}

absl::Status ProtocolDriverHoma::HandleConnect(
    std::string remote_connection_info, int peer) {
  CHECK_GE(peer, 0);
  CHECK_LT(static_cast<size_t>(peer), peer_addresses_.size());
  ServerAddress addr;
  if (!addr.ParseFromString(remote_connection_info)) {
    return absl::UnknownError(absl::StrCat(
        "remote_connection_info did not parse: ", remote_connection_info));
  }
  const char* const peer_ascii_addr = addr.ip_address().c_str();
  auto& peer_addr = peer_addresses_[peer];
  if (!strstr(addr.ip_address().c_str(), ":")) {
    peer_addr.in4.sin_family = AF_INET;
    peer_addr.in4.sin_port = htons(addr.port());
    char* const peer_in4_addr =
        reinterpret_cast<char*>(&peer_addr.in4.sin_addr);
    if (!inet_pton(AF_INET, peer_ascii_addr, peer_in4_addr)) {
      return absl::UnknownError(
          absl::StrCat("Peer address did not parse: ", peer_ascii_addr));
    }
  } else {
    peer_addr.in6.sin6_family = AF_INET6;
    peer_addr.in6.sin6_port = htons(addr.port());
    char* const peer_in6_addr =
        reinterpret_cast<char*>(&peer_addr.in6.sin6_addr);
    if (!inet_pton(AF_INET6, peer_ascii_addr, peer_in6_addr)) {
      return absl::UnknownError(
          absl::StrCat("Peer address did not parse: ", peer_ascii_addr));
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<std::string> ProtocolDriverHoma::HandlePreConnect(
    std::string_view remote_connection_info, int peer) {
  ServerAddress addr;
  addr.set_ip_address(server_ip_address_.ip());
  addr.set_port(server_port_);
  addr.set_socket_address(my_server_socket_address_);
  std::string ret;
  addr.AppendToString(&ret);
  return ret;
}

std::vector<TransportStat> ProtocolDriverHoma::GetTransportStats() {
  return {};
}

void ProtocolDriverHoma::ChurnConnection(int peer) {
  // Not required for Homa.
}

void ProtocolDriverHoma::ShutdownServer() {
  handler_set_.TryToNotify();
  if (shutting_down_server_.TryToNotify()) {
    for (size_t i = 0; i < server_threads_.size(); ++i) {
      // Initiate RPC to our own server sock, to wake up the server_thread:
      char buf[1] = {};
      uint64_t kernel_rpc_number;
      sockaddr_in_union loopback;
      socklen_t len = sizeof(loopback);
      if (getsockname(homa_server_sock_, &loopback.sa, &len) < 0) {
        LOG(ERROR) << absl::StrCat(strerror(errno),
                                   " getting sockname from server socket");
      }
      if (loopback.in6.sin6_family == AF_INET6) {
        loopback.in6.sin6_addr = in6addr_loopback;
      } else {
        loopback.in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      }

      int64_t res = homa_send(homa_server_sock_, buf, 1, &loopback,
                              &kernel_rpc_number, 0);
      if (res < 0) {
        LOG(INFO) << "homa_send result: " << res << " errno: " << errno
                  << " kernel_rpc_number " << kernel_rpc_number;
      }
    }
    for (auto& server_thread : server_threads_) {
      if (server_thread.joinable()) {
        server_thread.join();
      }
      if (server_buffer_) {
        munmap(server_buffer_, kHomaBufferSize);
        server_buffer_ = nullptr;
      }
    }
    close(homa_server_sock_);
    homa_server_sock_ = -1;
  }
}

void ProtocolDriverHoma::ShutdownClient() {
  if (shutting_down_client_.TryToNotify()) {
    for (size_t i = 0; i < client_completion_threads_.size(); ++i) {
      // Initiate RPC to our own client sock, then cancel it to wake up
      // the client_completion_thread:
      char buf[1] = {};
      uint64_t kernel_rpc_number;
      sockaddr_in_union loopback;
      socklen_t len = sizeof(loopback);
      if (getsockname(homa_client_sock_, &loopback.sa, &len) < 0) {
        LOG(ERROR) << absl::StrCat(strerror(errno),
                                   " getting sockname from client socket");
      }
      if (loopback.in6.sin6_family == AF_INET6) {
        loopback.in6.sin6_addr = in6addr_loopback;
      } else {
        loopback.in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      }
      LOG(INFO) << "sending a shutdown RPC";
      int64_t res = homa_send(homa_client_sock_, buf, 1, &loopback,
                              &kernel_rpc_number, 0);
      if (res < 0) {
        LOG(INFO) << "homa_send result: " << res << " errno: " << errno
                  << " kernel_rpc_number " << kernel_rpc_number;
      }

      LOG(INFO) << "cancelling that shutdown RPC";
      homa_abort(homa_client_sock_, kernel_rpc_number, EINTR);
    }

    for (auto& client_completion_thread : client_completion_threads_) {
      if (client_completion_thread.joinable()) {
        client_completion_thread.join();
      }
    }
    while (pending_rpcs_) {
      sched_yield();
    }
    if (client_buffer_) {
      munmap(client_buffer_, kHomaBufferSize);
      client_buffer_ = nullptr;
    }
    close(homa_client_sock_);
    homa_client_sock_ = -1;
  }
}

void ProtocolDriverHoma::InitiateRpc(int peer_index, ClientRpcState* state,
                                     std::function<void(void)> done_callback) {
  PendingHomaRpc* new_rpc = new PendingHomaRpc;

  new_rpc->done_callback = done_callback;
  new_rpc->state = state;
  new_rpc->serialized_request = SerializeToCord(&state->request, nocopy_);
  CHECK_EQ(new_rpc->serialized_request.size(), state->request.ByteSizeLong());
  if (new_rpc->serialized_request.empty()) {
    // Homa can't send a 0 byte message :(
    new_rpc->serialized_request = empty_message_placeholder;
  }
  std::vector<iovec> request_buf = Cord2Iovectors(new_rpc->serialized_request);
#ifdef THREAD_SANITIZER
  __tsan_release(new_rpc);
#endif

  ++pending_rpcs_;
  uint64_t kernel_rpc_number;

  int64_t res =
      homa_sendv(homa_client_sock_, request_buf.data(), request_buf.size(),
                 &peer_addresses_[peer_index], &kernel_rpc_number,
                 reinterpret_cast<uint64_t>(new_rpc));
  if (res < 0) {
    LOG(INFO) << "homa_send result: " << res << " errno: " << errno
              << " kernel_rpc_number " << kernel_rpc_number;
    delete new_rpc;
    --pending_rpcs_;
    state->success = false;
    done_callback();
  }
}

void ProtocolDriverHoma::ServerThread() {
  std::atomic<int> pending_responses = 0;
  homa::receiver server_receiver(homa_server_sock_, server_buffer_);

  handler_set_.WaitForNotification();
  while (1) {
    errno = 0;
    ssize_t msg_length = server_receiver.receive(HOMA_RECVMSG_REQUEST, 0);
    if (shutting_down_server_.HasBeenNotified()) {
      break;
    }
    int recv_errno = errno;
    if (msg_length < 0) {
      if (recv_errno != EINTR && recv_errno != EAGAIN) {
        LOG(ERROR) << "server homa_recv had an error: " << strerror(recv_errno);
      }
      continue;
    }
    if (msg_length == 0) {
      LOG(ERROR) << "server homa_recv got zero length request.";
      continue;
    }
    CHECK(server_receiver.is_request());
    const sockaddr_in_union src_addr = *server_receiver.src_addr();
    const uint64_t rpc_id = server_receiver.id();

    // ping-pong bypasses distbench_engine:
    if (ping_pong_) {
      struct iovec vecs[HOMA_MAX_BPAGES];
      ssize_t offset = 0;
      for (int i = 0; i < HOMA_MAX_BPAGES; ++i) {
        vecs[i].iov_base = server_receiver.get<char>(offset);
        vecs[i].iov_len = server_receiver.contiguous(offset);
        offset += vecs[i].iov_len;
        if (offset == msg_length) {
          homa_replyv(homa_server_sock_, vecs, i + 1, &src_addr, rpc_id);
          break;
        }
      }
      if (offset != msg_length) {
        LOG(FATAL) << "wtf? " << offset << " out of " << msg_length;
      }
      continue;
    }

    GenericRequestResponse* request = new GenericRequestResponse;
    if (!Parse(&server_receiver, msg_length, request, nocopy_)) {
      LOG(ERROR) << "rx_buf did not Parse as a GenericRequestResponse";
    }
    ServerRpcState* rpc_state = new ServerRpcState;
    rpc_state->request = request;
    rpc_state->SetFreeStateFunction([=]() {
      delete rpc_state->request;
      delete rpc_state;
    });
    rpc_state->SetSendResponseFunction([=, this, &pending_responses]() {
      absl::Cord buffer = SerializeToCord(&rpc_state->response, nocopy_);
      std::vector<iovec> buf = Cord2Iovectors(buffer);
      int64_t error;
      if (buf.empty()) {
        // Homa can't send a 0 byte message :(
        error = homa_reply(homa_server_sock_, empty_message_placeholder, 1,
                           &src_addr, rpc_id);
      } else {
        error = homa_replyv(homa_server_sock_, buf.data(), buf.size(),
                            &src_addr, rpc_id);
      }
      if (error) {
        LOG(ERROR) << "homa_reply for " << rpc_id
                   << " returned error: " << strerror(errno);
      }
      --pending_responses;
    });
    ++pending_responses;
    auto rpc_handler_thread_function = rpc_handler_(rpc_state);

    if (rpc_handler_thread_function) {
      actionlist_thread_pool_->AddTask(rpc_handler_thread_function);
    }
  }
  while (pending_responses) {
    sched_yield();
  }
}

void ProtocolDriverHoma::ClientCompletionThread(int thread_number) {
  homa::receiver client_receiver(homa_client_sock_, client_buffer_);
  while (!shutting_down_client_.HasBeenNotified() ||
         (pending_rpcs_ && !thread_number)) {
    errno = 0;
    ssize_t msg_length = client_receiver.receive(HOMA_RECVMSG_RESPONSE, 0);
    int recv_errno = errno;
    if (msg_length < 0) {
      if (recv_errno != EINTR && recv_errno != EAGAIN) {
        LOG(ERROR) << "homa_recv had an error: " << strerror(recv_errno);
      }
      continue;
    }

    PendingHomaRpc* pending_rpc =
        reinterpret_cast<PendingHomaRpc*>(client_receiver.completion_cookie());
#ifdef THREAD_SANITIZER
    __tsan_acquire(pending_rpc);
#endif
    CHECK(pending_rpc) << "Completion cookie was NULL";
    if (recv_errno || !msg_length) {
      pending_rpc->state->success = false;
    } else {
      pending_rpc->state->success = true;
      CHECK(!client_receiver.is_request());
      if (!Parse(&client_receiver, msg_length, &pending_rpc->state->response,
                 nocopy_)) {
        LOG(ERROR) << "rx_buf did not Parse as a GenericRequestResponse";
      }
    }
    pending_rpc->done_callback();
    --pending_rpcs_;
    delete pending_rpc;
  }
}

}  // namespace distbench
