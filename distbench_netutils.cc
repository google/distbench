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

#ifndef _GNU_SOURCE /* Pre-defined on GNU/Linux */
#define _GNU_SOURCE /* To get defns of NI_MAXSERV and NI_MAXHOST */
#endif

#include "distbench_netutils.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/strings/match.h"
#include "glog/logging.h"

ABSL_FLAG(bool, prefer_ipv4, false,
          "Prefer IPv4 addresses to IPv6 addresses when both are available");

namespace distbench {

std::vector<DeviceIpAddress> GetAllAddresses(std::string_view netdev) {
  struct ifaddrs* ifaddr;
  int family;
  char host[NI_MAXHOST];
  std::vector<DeviceIpAddress> result;

  if (getifaddrs(&ifaddr) == -1) return result;

  /* Walk through linked list, maintaining head pointer so we
     can free list later */
  for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) continue;

    family = ifa->ifa_addr->sa_family;
    if (family == AF_PACKET) continue;

    int s;
    s = getnameinfo(ifa->ifa_addr,
                    (family == AF_INET) ? sizeof(struct sockaddr_in)
                                        : sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
    if (s == 0) {  // Success
      DeviceIpAddress ip_address(host, ifa->ifa_name, family);
      if (netdev.empty() || netdev == ip_address.netdevice()) {
        result.emplace_back(ip_address);
      }
    }
  }

  freeifaddrs(ifaddr);
  return result;
}

absl::StatusOr<DeviceIpAddress> GetBestAddress(std::string_view netdev) {
  return GetBestAddress(netdev, absl::GetFlag(FLAGS_prefer_ipv4));
}

absl::StatusOr<DeviceIpAddress> GetBestAddress(std::string_view netdev,
                                               bool prefer_ipv4) {
  auto all_addresses = GetAllAddresses(netdev);

  if (!netdev.empty()) {
    // Exact match
    for (const auto& address : all_addresses) {
      if (!address.isLinkLocal() && address.isIPv4() == prefer_ipv4) {
        return address;
      }
    }

    // Match the device with any IP type
    for (const auto& address : all_addresses) {
      if (!address.isLinkLocal()) {
        LOG(WARNING) << "Using " << address.ToString()
                     << " which is not of the preferred ip type (v4/v6).";
        return address;
      }
    }

    return absl::NotFoundError(
        absl::StrCat("No address found for netdev '", netdev,
                     "' (prefer_ipv4=", prefer_ipv4, ")"));
  }

  int score = -1;
  DeviceIpAddress best_match;
  for (const auto& address : all_addresses) {
    if (address.isLinkLocal() || address.isLoopback()) {
      continue;
    }
    int cur_score = (address.isIPv4() == prefer_ipv4);
    // High speed networks in cloudlab are on a private IP range.
    cur_score += address.isPrivate();
    if (cur_score > score) {
      score = cur_score;
      best_match = address;
    }
  }

  if (score < 0) {
    return absl::NotFoundError(absl::StrCat(
        "No address found for any netdev (prefer_ipv4=", prefer_ipv4, ")"));
  }

  return best_match;
}

bool DeviceIpAddress::isIPv4() const { return net_family_ == AF_INET; }

bool DeviceIpAddress::isLoopback() const { return device_ == "lo"; }

bool DeviceIpAddress::isPrivate() const {
  return absl::StartsWith(ip_, "10.") ||
         // Technically this should go all the way to 172.31:
         absl::StartsWith(ip_, "172.16") || absl::StartsWith(ip_, "192.168") ||
         absl::StartsWith(ip_, "fc00:") || absl::StartsWith(ip_, "fd00:");
}

bool DeviceIpAddress::isLinkLocal() const {
  return absl::StartsWith(ip_, "fe80:") || absl::StartsWith(ip_, "169.254.255");
}

std::string DeviceIpAddress::ToString() const {
  std::string ret = ip_ + " on " + device_ + " ";
  if (isIPv4()) {
    ret += "(ipv4)";
  } else {
    ret += "(ipv6)";
  }
  return ret;
}

std::string DeviceIpAddress::ToStringForURI() const {
  if (isIPv4())
    return ip_;
  else
    return "[" + ip_ + "]";
}

namespace {

bool HasOnlyIPv4(std::string_view netdev) {
  for (const auto& address : GetAllAddresses(netdev)) {
    if (!address.isIPv4() && !address.isLinkLocal()) return false;
  }
  return true;
}

}  // namespace

std::string GetBindAddressFromPort(std::string_view netdev, int port) {
  absl::StatusOr<DeviceIpAddress> addr = GetBestAddress(netdev);

  if (netdev.empty() || !addr.ok()) {
    if (HasOnlyIPv4(netdev) || absl::GetFlag(FLAGS_prefer_ipv4))
      return absl::StrCat("0.0.0.0:", port);
    else
      return absl::StrCat("[::]:", port);
  }
  return SocketAddressForIp(addr.value(), port);
}

absl::StatusOr<DeviceIpAddress> IpAddressForDevice(std::string_view netdev,
                                                   int ip_version) {
  if (ip_version == 4) {
    auto res = GetBestAddress(netdev, true);
    if (res.ok() && !res.value().isIPv4()) {
      return absl::NotFoundError(
          absl::StrCat("No IPv4 address found for netdev '", netdev, "'"));
    }
    return res;
  } else if (ip_version == 6) {
    auto res = GetBestAddress(netdev, false);
    if (res.ok() && res.value().isIPv4()) {
      return absl::NotFoundError(
          absl::StrCat("No IPv6 address found for netdev '", netdev, "'"));
    }
    return res;
  } else {
    return GetBestAddress(netdev);
  }
}

absl::StatusOr<std::string> SocketAddressForDevice(std::string_view netdev,
                                                   int port) {
  auto maybe_address = GetBestAddress(netdev);
  if (!maybe_address.ok()) return maybe_address.status();
  return SocketAddressForIp(maybe_address.value(), port);
}

std::string SocketAddressForIp(DeviceIpAddress ip, int port) {
  if (ip.isIPv4()) {
    return absl::StrCat(ip.ip(), ":", port);
  }
  return absl::StrCat("[", ip.ip(), "]:", port);
}

};  // namespace distbench
