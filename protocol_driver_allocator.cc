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
#include "glog/logging.h"

namespace distbench {

int max_protocol_driver_tree_depth_ = 4;

std::function<absl::StatusOr<ProtocolDriverOptions>(
    const std::string&)>
    alias_resolver_;

void SetProtocolDriverAliasResolver(std::function<absl::StatusOr<ProtocolDriverOptions>(
    const std::string&)> alias_resolver) {
  alias_resolver_ = alias_resolver;
}

absl::StatusOr<std::unique_ptr<ProtocolDriver>> AllocateProtocolDriver(
    ProtocolDriverOptions opts, int* port, int tree_depth) {
  LOG(INFO) << "AllocateProtocolDriver called with protocol_name: '"<< opts.protocol_name() << "'";
  if (tree_depth == max_protocol_driver_tree_depth_) {
    return absl::FailedPreconditionError(
      absl::StrCat("Tree cannot be deeper than max depth of: ",
                   max_protocol_driver_tree_depth_, "."));
  }
  std::unique_ptr<ProtocolDriver> pd;
  if (opts.protocol_name() == "grpc" ||
      opts.protocol_name() == "grpc_async_callback") {
    pd = std::make_unique<ProtocolDriverGrpc>();
  } else {
    if (alias_resolver_ == nullptr) {
      return absl::InvalidArgumentError(
        "Protocol driver alias resolver function is not set.");
    }
    auto maybe_resolved_opts = alias_resolver_(opts.protocol_name());
    if (!maybe_resolved_opts.ok()) return maybe_resolved_opts.status();
    opts = maybe_resolved_opts.value();
    auto maybe_pd = AllocateProtocolDriver(opts, port, tree_depth+1);
    if (!maybe_pd.ok()) return maybe_pd.status();
    pd = std::move(maybe_pd.value());
  }
  absl::Status ret = pd->Initialize(opts, port);
  if (!ret.ok()) {
    return ret;
  } else {
    return pd;
  }
}

}  // namespace distbench
