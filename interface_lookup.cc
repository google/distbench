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

#include "interface_lookup.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool GetFirstAddress(net_base::IPAddress& ip_address, int which_family) {
  struct ifaddrs* ifaddr;
  int family;
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddr) == -1) return false;

  /* Walk through linked list, maintaining head pointer so we
     can free list later */
  for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) continue;

    family = ifa->ifa_addr->sa_family;
    if (family == AF_PACKET) continue;
    if (strcmp(ifa->ifa_name, "lo") == 0) continue;

    if (family == which_family) {
      int s;
      s = getnameinfo(ifa->ifa_addr,
                      (family == AF_INET) ? sizeof(struct sockaddr_in)
                                          : sizeof(struct sockaddr_in6),
                      host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
      if (s != 0) {
        freeifaddrs(ifaddr);
        return false;
      } else {
        ip_address.from_c_str(host);
        freeifaddrs(ifaddr);
        return true;
      }
    }
  }

  freeifaddrs(ifaddr);
  return false;
}

bool net_base::InterfaceLookup::MyIPv4Address(IPAddress* addr) {
  return GetFirstAddress(*addr, AF_INET);
}

bool net_base::InterfaceLookup::MyIPv6Address(IPAddress* addr) {
  return GetFirstAddress(*addr, AF_INET6);
}
