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

#include "absl/strings/match.h"
#include "glog/logging.h"

namespace distbench {

std::vector<DeviceIpAddress> GetAllAddresses() {
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
      result.emplace_back(ip_address);
    }
  }

  freeifaddrs(ifaddr);
  return result;
}

absl::StatusOr<DeviceIpAddress> GetBestAddress(bool prefer_ipv4,
                                               std::string_view netdev) {
  auto all_addresses = GetAllAddresses();

  // Exact match
  for (const auto& address : all_addresses) {
    if (address.isIPv4() == prefer_ipv4 && address.netdevice() == netdev) {
      return address;
    }
  }

  // Match the device with any IP type
  for (const auto& address : all_addresses) {
    if (address.netdevice() == netdev) {
      LOG(WARNING) << "Using " << address.ToString()
                   << " which is not of the favorite ip type (v4/v6).";
      return address;
    }
  }

  if (!netdev.empty()) {
    return absl::NotFoundError(
        absl::StrCat("No address found for netdev '", netdev,
                     "' (prefer_ipv4=", prefer_ipv4, ")"));
  }

  int score = 0;
  DeviceIpAddress best_match;
  for (const auto& address : all_addresses) {
    int cur_score = 0;
    // Skip link-local addresses:
    if (absl::StartsWith(address.ToString(), "fe80:")) {
      continue;
    }
    if (address.isIPv4() == prefer_ipv4) {
      cur_score += 2048;
    }
    if (address.netdevice() != "lo") {
      cur_score += 1024;
    }
    if (cur_score > score) {
      score = cur_score;
      best_match = address;
    }
  }

  if (score == 0) {
    return absl::NotFoundError(absl::StrCat(
        "No address found for any netdev (prefer_ipv4=", prefer_ipv4, ")"));
  }

  return best_match;
}

bool DeviceIpAddress::isIPv4() const { return net_family_ == AF_INET; }

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

};  // namespace distbench
