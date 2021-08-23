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
#include "protocol_driver_grpc_async_callback.h"


namespace distbench {

absl::StatusOr<std::unique_ptr<ProtocolDriver>> AllocateProtocolDriver(
    const ProtocolDriverOptions& opts) {
  if (opts.protocol_name() == "grpc") {
    return std::make_unique<ProtocolDriverGrpc>();
  } else if (opts.protocol_name() == "grpc_async_callback") {
    return std::make_unique<ProtocolDriverGrpcAsyncCallback>();
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Unknown protocol_name: ", opts.protocol_name()));
}

}  // namespace distbench
