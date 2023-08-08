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

#ifndef DISTBENCH_PROTOCOL_DRIVER_GRPC_H_
#define DISTBENCH_PROTOCOL_DRIVER_GRPC_H_

#include "distbench.grpc.pb.h"
#include "distbench_netutils.h"
#include "distbench_thread_support.h"
#include "distbench_threadpool.h"
#include "distbench_utils.h"
#include "protocol_driver.h"

namespace distbench {

class GrpcCommonClientDriver : public ProtocolDriverClient {
 public:
  GrpcCommonClientDriver();
  ~GrpcCommonClientDriver() override;

  void SetNumPeers(int num_peers) override;
  void SetNumMultiServerChannels(int num_channels) override;

  // Allocate local resources that are needed to establish a connection
  // E.g. an unconnected RoCE QueuePair. Returns opaque data. If no local
  // resources are needed, this is a NOP.
  // absl::StatusOr<std::string> Preconnect() override;

  // Actually establish a conection, given the opaque data from the
  // the responder. E.g. connect the local and remote RoCE queue pairs.
  absl::Status HandleConnect(std::string remote_connection_info,
                             int peer) override;
  absl::Status SetupMultiServerChannel(
      const ::google::protobuf::RepeatedPtrField<NamedSetting>& settings,
      const std::vector<int>& peer_ids, int channel_id) override;

 protected:
  std::vector<std::string> peer_connection_info_;
  std::string transport_;
  std::vector<std::unique_ptr<Traffic::Stub>> grpc_client_stubs_;
  std::vector<std::unique_ptr<Traffic::Stub>> multiserver_stubs_;
  std::atomic<int> pending_rpcs_ = 0;
};

class GrpcPollingClientDriver : public GrpcCommonClientDriver {
 public:
  GrpcPollingClientDriver();
  ~GrpcPollingClientDriver() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts) override;

  void InitiateRpcToMultiServerChannel(
      int channel_index, ClientRpcState* state,
      std::function<void(void)> done_callback) override;

  void InitiateRpc(int peer_index, ClientRpcState* state,
                   std::function<void(void)> done_callback) override;
  void ChurnConnection(int peer) override;
  void ShutdownClient() override;

  virtual std::vector<TransportStat> GetTransportStats() override;

 private:
  void RpcCompletionThread();
  absl::Notification shutdown_;
  std::thread cq_poller_;
  grpc::CompletionQueue cq_;
};

class GrpcCallbackClientDriver : public GrpcCommonClientDriver {
 public:
  GrpcCallbackClientDriver();
  ~GrpcCallbackClientDriver() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts) override;

  void InitiateRpcToMultiServerChannel(
      int channel_index, ClientRpcState* state,
      std::function<void(void)> done_callback) override;

  void InitiateRpc(int peer_index, ClientRpcState* state,
                   std::function<void(void)> done_callback) override;
  void ChurnConnection(int peer) override;
  void ShutdownClient() override;

  virtual std::vector<TransportStat> GetTransportStats() override;
};

class GrpcCommonServerDriver : public ProtocolDriverServer {
 public:
  GrpcCommonServerDriver(){};
  ~GrpcCommonServerDriver() override{};

  absl::StatusOr<std::string> HandlePreConnect(
      std::string_view remote_connection_info, int peer) override;
  void HandleConnectFailure(std::string_view local_connection_info) override;

 protected:
  std::unique_ptr<grpc::Server> server_;
  int server_port_ = 0;
  DeviceIpAddress server_ip_address_;
  std::string server_socket_address_;
  std::string transport_;
};

class GrpcInlineServerDriver : public GrpcCommonServerDriver {
 public:
  GrpcInlineServerDriver();
  ~GrpcInlineServerDriver() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;

  void SetHandler(std::function<std::function<void()>(ServerRpcState* state)>
                      handler) override;
  void ShutdownServer() override;
  std::vector<TransportStat> GetTransportStats() override;

 private:
  std::unique_ptr<Traffic::Service> traffic_service_;
};

class GrpcHandoffServerDriver : public GrpcCommonServerDriver {
 public:
  GrpcHandoffServerDriver();
  ~GrpcHandoffServerDriver() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;

  void SetHandler(std::function<std::function<void()>(ServerRpcState* state)>
                      handler) override;
  void ShutdownServer() override;
  std::vector<TransportStat> GetTransportStats() override;

 private:
  std::unique_ptr<Traffic::ExperimentalCallbackService> traffic_service_;
};

class GrpcPollingServerDriver : public GrpcCommonServerDriver {
 public:
  GrpcPollingServerDriver(std::unique_ptr<AbstractThreadpool> tp);
  ~GrpcPollingServerDriver() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;

  void SetHandler(std::function<std::function<void()>(ServerRpcState* state)>
                      handler) override;
  void ShutdownServer() override;
  std::vector<TransportStat> GetTransportStats() override;
  void ProcessGenericRpc(GenericRequest* request, GenericResponse* response);
  void HandleRpcs();

 private:
  std::unique_ptr<grpc::ServerCompletionQueue> server_cq_;
  std::unique_ptr<Traffic::AsyncService> traffic_async_service_;
  grpc::ServerContext context;
  std::function<std::function<void()>(ServerRpcState* state)> handler_;
  std::unique_ptr<AbstractThreadpool> thread_pool_;
  SafeNotification server_shutdown_detected_;
  absl::Notification handle_rpcs_started_;
  SafeNotification handler_set_;
  std::thread handle_rpcs_;
};

class ProtocolDriverGrpc : public ProtocolDriver {
 public:
  ProtocolDriverGrpc();
  ~ProtocolDriverGrpc() override;

  absl::Status Initialize(const ProtocolDriverOptions& pd_opts,
                          int* port) override;

  void SetHandler(std::function<std::function<void()>(ServerRpcState* state)>
                      handler) override;
  void SetNumPeers(int num_peers) override;
  void SetNumMultiServerChannels(int num_channels) override;

  // Connects to the actual GRPC service.
  absl::Status HandleConnect(std::string remote_connection_info,
                             int peer) override;

  // Returns the address of the GRPC service.
  absl::StatusOr<std::string> HandlePreConnect(
      std::string_view remote_connection_info, int peer) override;
  void HandleConnectFailure(std::string_view local_connection_info) override;

  absl::Status SetupMultiServerChannel(
      const ::google::protobuf::RepeatedPtrField<NamedSetting>& settings,
      const std::vector<int>& peer_ids, int channel_id) override;

  void InitiateRpcToMultiServerChannel(
      int channel_index, ClientRpcState* state,
      std::function<void(void)> done_callback) override;

  void InitiateRpc(int peer_index, ClientRpcState* state,
                   std::function<void(void)> done_callback) override;
  void ChurnConnection(int peer) override;
  void ShutdownServer() override;
  void ShutdownClient() override;

  std::vector<TransportStat> GetTransportStats() override;

 private:
  std::unique_ptr<distbench::GrpcCommonClientDriver> client_;
  std::unique_ptr<distbench::GrpcCommonServerDriver> server_;
};

}  // namespace distbench

#endif  // DISTBENCH_PROTOCOL_DRIVER_GRPC_H_
