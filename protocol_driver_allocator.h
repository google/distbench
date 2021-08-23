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

#ifndef DISTBENCH_PROTOCOL_DRIVER_ALLOCATOR_H_
#define DISTBENCH_PROTOCOL_DRIVER_ALLOCATOR_H_

#include "distbench.pb.h"
#include "protocol_driver.h"

namespace distbench {

absl::StatusOr<std::unique_ptr<ProtocolDriver>> AllocateProtocolDriver(
    const ProtocolDriverOptions& opts);

}  // namespace distbench

#endif  //  DISTBENCH_PROTOCOL_DRIVER_ALLOCATOR_H_
