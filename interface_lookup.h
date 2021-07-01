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

#ifndef INTERFACELOOKUP_INTERFACE_LOOKUP_H_
#define INTERFACELOOKUP_INTERFACE_LOOKUP_H_

#include <string>

namespace net_base{
class IPAddress{
 private:
  std::string address;

 public:
  std::string ToString() const{return this->address;}
  void from_c_str(const char *s_address) {
    this->address = std::string(s_address);
  }
};

namespace InterfaceLookup{

bool MyIPv4Address(IPAddress *addr);
bool MyIPv6Address(IPAddress *addr);

};  // namespace InterfaceLookup
};  // namespace net_base

#endif  // INTERFACELOOKUP_INTERFACE_LOOKUP_H_
