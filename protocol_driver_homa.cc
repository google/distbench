#include "protocol_driver_homa.h"

#include <arpa/inet.h>

#include "distbench_utils.h"
#include "external/homa_module/homa.h"
#include "glog/logging.h"

namespace distbench {

///////////////////////////////////
// ProtocolDriverHoma Methods //
///////////////////////////////////

ProtocolDriverHoma::ProtocolDriverHoma() {}

absl::Status ProtocolDriverHoma::Initialize(
    const ProtocolDriverOptions& pd_opts, int* port) {
  VLOG(1) << "ProtocolDriverOptions: " << pd_opts.DebugString();

  if (pd_opts.has_netdev_name()) {
    netdev_name_ = pd_opts.netdev_name();
  }

  for (const auto& setting : pd_opts.server_settings()) {
    if (setting.name() == "client_threads") {
      client_run_threads_ = setting.int64_value();
      if (client_run_threads_ < 1) {
        return absl::InvalidArgumentError(
            "client_threads int64_value field must be positive ");
      }
    } else if (setting.name() == "server_threads") {
      server_run_threads_ = setting.int64_value();
      if (server_run_threads_ < 1) {
        return absl::InvalidArgumentError(
            "server_threads int64_value field must be positive ");
      }
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown protocol driver option: ", setting.name()));
    }
  }

  auto maybe_ip = IpAddressForDevice(netdev_name_);
  if (!maybe_ip.ok()) return maybe_ip.status();
  server_ip_address_ = maybe_ip.value();
  int af = server_ip_address_.Family();

  homa_client_sock_ = socket(af, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_HOMA);
  if (homa_client_sock_ < 0) {
    return absl::UnknownError(
        absl::StrCat(strerror(errno), " creating client homa socket"));
  }
  homa_server_sock_ = socket(af, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_HOMA);
  if (homa_server_sock_ < 0) {
    return absl::UnknownError(
        absl::StrCat(strerror(errno), " creating server homa socket"));
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
        absl::StrCat(strerror(errno), " getting sockname from socket"));
  }

  char addr_string[256];
  if (server_sock_addr.in4.sin_family == AF_INET) {
    *port = ntohs(server_sock_addr.in4.sin_port);
    inet_ntop(AF_INET, &server_sock_addr.in4.sin_addr, addr_string,
              sizeof(addr_string));
    my_server_socket_address_ = absl::StrCat(addr_string, ":", *port);
  } else if (server_sock_addr.in6.sin6_family == AF_INET6) {
    *port = ntohs(server_sock_addr.in6.sin6_port);
    inet_ntop(AF_INET6, &server_sock_addr.in6.sin6_addr, addr_string,
              sizeof(addr_string));
    my_server_socket_address_ = absl::StrCat("[", addr_string, "]:", *port);
  } else {
    return absl::UnknownError(
        absl::StrCat("Unknown address family for homa socket: ",
                     server_sock_addr.in4.sin_family));
  }
  server_port_ = *port;

  client_completion_thread_ = RunRegisteredThread(
      "HomaClient", [=]() { this->ClientCompletionThread(); });
  server_thread_ =
      RunRegisteredThread("HomaServer", [=]() { this->ServerThread(); });
  return absl::OkStatus();
}

ProtocolDriverHoma::~ProtocolDriverHoma() {
  ShutdownServer();
  ShutdownClient();
}

void ProtocolDriverHoma::SetHandler(
    std::function<std::function<void()>(ServerRpcState* state)> handler) {
  rpc_handler_ = handler;
  handler_set_.Notify();
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

  VLOG(1) << "Homa: HandlePreConnect: "
          << "peer: " << peer << ", ip_address: " << addr.ip_address()
          << ", port: " << addr.port()
          << ", socket_address: " << addr.socket_address() << ".";

  return ret;
}

std::vector<TransportStat> ProtocolDriverHoma::GetTransportStats() {
  return {};
}

void ProtocolDriverHoma::ChurnConnection(int peer) {
  // Not required for Homa.
}

void ProtocolDriverHoma::ShutdownServer() {
  if (!handler_set_.HasBeenNotified()) handler_set_.Notify();
  if (!shutting_down_server_.HasBeenNotified()) {
    shutting_down_server_.Notify();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    close(homa_server_sock_);
    homa_server_sock_ = -1;
  }
}

void ProtocolDriverHoma::ShutdownClient() {
  if (!shutting_down_client_.HasBeenNotified()) {
    shutting_down_client_.Notify();
    if (client_completion_thread_.joinable()) {
      client_completion_thread_.join();
    }
    while (pending_rpcs_) {
      sched_yield();
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
  new_rpc->serialized_request = "?";  // Homa can't send a 0 byte message :(
  state->request.AppendToString(&new_rpc->serialized_request);
#ifdef THREAD_SANITIZER
  __tsan_release(new_rpc);
#endif

  ++pending_rpcs_;
  uint64_t kernel_rpc_number;

  int64_t res = homa_send(
      homa_client_sock_, new_rpc->serialized_request.data(),
      new_rpc->serialized_request.size(), &peer_addresses_[peer_index],
      &kernel_rpc_number, reinterpret_cast<uint64_t>(new_rpc));
  if (res < 0) {
    LOG(INFO) << "homa_send result: " << res << " errno: " << errno
              << " kernel_rpc_number " << kernel_rpc_number;
    delete new_rpc;
    state->success = false;
    done_callback();
  }
}

void ProtocolDriverHoma::ServerThread() {
  std::atomic<int> pending_actionlist_threads = 0;

  handler_set_.WaitForNotification();
  while (!shutting_down_server_.HasBeenNotified()) {
    sockaddr_in_union src_addr;
    uint64_t rpc_id = 0;
    char rx_buf[1048576];
    size_t length = 0;
    errno = 0;
    int64_t error = homa_recv(homa_server_sock_, rx_buf, sizeof(rx_buf),
                                     HOMA_RECV_NONBLOCKING | HOMA_RECV_REQUEST,
                                     &src_addr, &rpc_id, &length, NULL);
    int recv_errno = errno;
    if (error < 0) {
      if (recv_errno != EINTR && recv_errno != EAGAIN) {
        LOG(ERROR) << "homa_recv had an error: " << strerror(recv_errno);
      }
      continue;
    }
    GenericRequest* request = new GenericRequest;
    if (!request->ParseFromArray(rx_buf + 1, length - 1)) {
      LOG(FATAL) << "rx_buf did not parse as a GenericRequest";
    }
    ServerRpcState* rpc_state = new ServerRpcState;
    rpc_state->request = request;
    rpc_state->SetFreeStateFunction([=]() {
      delete rpc_state->request;
      delete rpc_state;
    });
    rpc_state->SetSendResponseFunction([=, &pending_actionlist_threads]() {
      std::string txbuf = "!";  // Homa can't send a 0 byte message :(
      rpc_state->response.AppendToString(&txbuf);
      int64_t error = homa_reply(homa_server_sock_, txbuf.c_str(),
                                        txbuf.length(), &src_addr, rpc_id);
      if (error) {
        LOG(ERROR) << "homa_reply for " << rpc_id
                   << " returned error: " << strerror(errno);
      }
      --pending_actionlist_threads;
    });
    auto fct_action_list_thread = rpc_handler_(rpc_state);
    ++pending_actionlist_threads;
    if (fct_action_list_thread)
      RunRegisteredThread("DedicatedActionListThread", fct_action_list_thread)
          .detach();
  }
  while (pending_actionlist_threads) {
    sched_yield();
  }
}

void ProtocolDriverHoma::ClientCompletionThread() {
  while (!shutting_down_client_.HasBeenNotified() || pending_rpcs_) {
    sockaddr_in_union src_addr;
    uint64_t rpc_id = 0;
    uint64_t cookie = 0;
    char rx_buf[1048576];
    size_t length = 0;
    errno = 0;
    int64_t error = homa_recv(homa_client_sock_, rx_buf, sizeof(rx_buf),
                              HOMA_RECV_NONBLOCKING | HOMA_RECV_RESPONSE,
                              &src_addr, &rpc_id, &length, &cookie);
    int recv_errno = errno;
    if (error < 0) {
      if (recv_errno == EINTR || recv_errno == EAGAIN) {
        continue;
      }
      if (recv_errno != ECANCELED) {
        LOG(ERROR) << "homa_recv had an error: " << strerror(recv_errno);
      }
    }
    PendingHomaRpc* pending_rpc = reinterpret_cast<PendingHomaRpc*>(cookie);
#ifdef THREAD_SANITIZER
    __tsan_acquire(pending_rpc);
#endif
    CHECK(pending_rpc) << "Completion cookie was NULL";
    if (recv_errno || !length) {
      pending_rpc->state->success = false;
    } else {
      pending_rpc->state->success = true;
      if (!pending_rpc->state->response.ParseFromArray(rx_buf + 1,
                                                       length - 1)) {
        LOG(FATAL) << "rx_buf did not parse as a GenericResponse";
      }
    }
    pending_rpc->done_callback();
    --pending_rpcs_;
    delete pending_rpc;
  }
}

}  // namespace distbench
