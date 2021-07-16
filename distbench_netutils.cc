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

#ifndef _GNU_SOURCE     /* Pre-defined on GNU/Linux */
#define _GNU_SOURCE     /* To get defns of NI_MAXSERV and NI_MAXHOST */
#endif

#include "distbench_netutils.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/if_link.h>
#include <string.h>

#include "glog/logging.h"

#include <string>
#include <vector>

namespace distbench {

std::vector<IPAddressWithInfos> GetAllAddresses() {
  struct ifaddrs *ifaddr;
  int family, s;
  char host[NI_MAXHOST];
  std::vector<IPAddressWithInfos> result;

  if (getifaddrs(&ifaddr) == -1)
    return result;

  /* Walk through linked list, maintaining head pointer so we
     can free list later */
  for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;

    family = ifa->ifa_addr->sa_family;
    if (family == AF_PACKET)
      continue;

    s = getnameinfo(ifa->ifa_addr,
                    (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                          sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
    if (s == 0) { // Success
      IPAddressWithInfos ip_address(host, ifa->ifa_name, family);
      result.push_back(ip_address);
    }
  }

  freeifaddrs(ifaddr);
  return result;
}

IPAddressWithInfos GetBestAddress(bool prefer_ipv4, std::string_view netdev) {
  auto all_addresses = GetAllAddresses();

  // Exact match
  for (const auto& address: all_addresses) {
    if (address.isIPv4() == prefer_ipv4 &&
        address.netdevice() == netdev)
      return address;
  }

  int score = 0;
  IPAddressWithInfos best_match("127.0.0.1", "lo", AF_INET);
  for (const auto& address: all_addresses) {
    int cur_score = 0;
    if (address.isIPv4() == prefer_ipv4)
      cur_score += 2048;
    if (address.netdevice() == netdev)
      cur_score += 4096;
    if (address.netdevice() != "lo")
      cur_score += 1024;
    if (cur_score > score) {
      score = cur_score;
      best_match = address;
    }
  }

  LOG(WARNING) << "No perfect match found, using " << best_match.ToString()
               << "(score=" << score << ")";
  return best_match;
}

bool IPAddressWithInfos::isIPv4() const {
  return net_family_ == AF_INET;
}

std::string IPAddressWithInfos::ToString() const {
  std::string ret = ip_ + " on " + device_ + " ";
  if (isIPv4())
    ret += "(ipv4)";
  else
    ret += "(ipv6)";
  return ret;
};

};  // namespace distbench
