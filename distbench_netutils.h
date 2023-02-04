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

#ifndef DISTBENCH_DISTBENCH_NETUTILS_H_
#define DISTBENCH_DISTBENCH_NETUTILS_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace distbench {

class DeviceIpAddress {
 public:
  DeviceIpAddress() {}
  DeviceIpAddress(const char* host, const char* devname, int family) {
    ip_ = std::string(host);
    device_ = std::string(devname);
    net_family_ = family;
  }

  bool isIPv4() const;
  bool isLoopback() const;
  bool isLinkLocal() const;
  bool isPrivate() const;
  int Family() const { return net_family_; }
  std::string ToString() const;
  std::string ToStringForURI() const;
  std::string netdevice() const { return device_; }
  std::string ip() const { return ip_; }

 private:
  std::string ip_;
  std::string device_;
  int net_family_;
};

std::vector<DeviceIpAddress> GetAllAddresses(std::string_view netdev);
absl::StatusOr<DeviceIpAddress> GetBestAddress(std::string_view netdev);
absl::StatusOr<DeviceIpAddress> GetBestAddress(std::string_view netdev,
                                               bool prefer_ipv4);
std::string GetBindAddressFromPort(std::string_view netdev, int port);
absl::StatusOr<DeviceIpAddress> IpAddressForDevice(std::string_view netdev,
                                                   int ip_version = 0);
std::string SocketAddressForIp(DeviceIpAddress ip, int port);
absl::StatusOr<std::string> SocketAddressForDevice(std::string_view netdev,
                                                   int port);

};  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_NETUTILS_H_
