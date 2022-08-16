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

#include "protocol_driver_allocator.h"

#include "protocol_driver_grpc.h"

namespace distbench {

int max_protocol_driver_tree_depth_ = 4;

std::function<absl::StatusOr<ProtocolDriverOptions>(
    const std::string& protocol_name)>
    resolver_;

void SetProtocolDriverResolver(std::function<absl::StatusOr<ProtocolDriverOptions>(
    const std::string& protocol_name)> resolver) {
  resolver_ = resolver;
}

absl::StatusOr<std::unique_ptr<ProtocolDriver>> AllocateProtocolDriver(
    const ProtocolDriverOptions& opts, int* port, int tree_depth) {

  if (tree_depth == max_protocol_driver_tree_depth_) {
    return absl::InvalidArgumentError(
      absl::StrCat("Tree cannot be deeper than max depth of: ",
                   max_protocol_driver_tree_depth_, "."));
  }
  std::unique_ptr<ProtocolDriver> pd;
  absl::Status ret;
  if (opts.protocol_name() == "grpc" ||
      opts.protocol_name() == "grpc_async_callback") {
    pd = std::make_unique<ProtocolDriverGrpc>();
    ret = pd->Initialize(opts, port);
  } else {
    if (resolver_ == nullptr) {
      return absl::InvalidArgumentError(
        "Protocol Resolver Function is not set.");
    }
    auto maybe_resolved_opts = resolver_(opts.protocol_name());
    if (!maybe_resolved_opts.ok()) return maybe_resolved_opts.status();
    auto resolved_opts = maybe_resolved_opts.value();
    pd = std::make_unique<ProtocolDriverGrpc>(); // For now, because ProtocolDriver has only virtual methods
    ret = pd->Initialize(resolved_opts, port);
  }
  if (!ret.ok()) {
    return ret;
  } else {
    return pd;
  }
}

}  // namespace distbench
