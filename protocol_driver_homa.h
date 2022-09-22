#ifndef DISTBENCH_PROTOCOL_DRIVER_HOMA_H_
#define DISTBENCH_PROTOCOL_DRIVER_HOMA_H_

#include "distbench_utils.h"
#include "external/homa_module/homa.h"
#include "protocol_driver.h"

namespace distbench {

struct PendingHomaRpc {
  ClientRpcState* state;
  std::string serialized_request;
  std::function<void(void)> done_callback;
};

class ProtocolDriverHoma : public ProtocolDriver {
 public:
  ProtocolDriverHoma();

  ~ProtocolDriverHoma() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;

  void SetHandler(std::function<std::function<void()>(ServerRpcState* state)>
                      handler) override;

  void SetNumPeers(int num_peers) override;

  absl::Status HandleConnect(std::string remote_connection_info,
                             int peer) override;

  absl::StatusOr<std::string> HandlePreConnect(
      std::string_view remote_connection_info, int peer) override;

  std::vector<TransportStat> GetTransportStats() override;

  void InitiateRpc(int peer_index, ClientRpcState* state,
                   std::function<void(void)> done_callback) override;

  void ChurnConnection(int peer) override;

  void ShutdownServer() override;

  void ShutdownClient() override;

 private:
  void ClientCompletionThread();
  void ServerThread();

  int homa_client_sock_ = -1;
  int homa_server_sock_ = -1;
  int server_port_ = 0;
  DeviceIpAddress server_ip_address_;
  std::string my_server_socket_address_;

  // Homa RPC Server.
  int server_run_threads_ = 1;

  // Homa RPC Client.
  int client_run_threads_ = 1;
  std::atomic<int> pending_rpcs_ = 0;

  std::string netdev_name_;
  std::thread client_completion_thread_;
  std::thread server_thread_;
  SafeNotification handler_set_;
  SafeNotification shutting_down_server_;
  SafeNotification shutting_down_client_;

  std::function<std::function<void()>(ServerRpcState* state)> rpc_handler_;

  std::vector<sockaddr_in_union> peer_addresses_;
};

}  // namespace distbench

#endif  // DISTBENCH_PROTOCOL_DRIVER_HOMA_H_
