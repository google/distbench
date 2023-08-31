// Copyright 2023 Google LLC
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

#ifndef DISTBENCH_DISTBENCH_PAYLOAD_H_
#define DISTBENCH_DISTBENCH_PAYLOAD_H_

#include <sys/uio.h>

#include "distbench.pb.h"

namespace distbench {

class PayloadAllocator {
 public:
  virtual ~PayloadAllocator() = default;
  virtual absl::Cord AllocPayloadCord(size_t size);
  void AddPadding(GenericRequestResponse* msg, size_t target_size);
};

bool ReadVarInt(const char** start, const char* end, int64_t* out);

std::string MakeVarInt(uint64_t val);

int VarIntSize(uint64_t val);

// Returns the number of bytes of payload to add in order to achieve the given
// target_size. If the size is already larger than the target size this returns
// zero.
size_t GetPaddingForSerializedSize(GenericRequestResponse* msg,
                                   size_t target_size);

size_t MetaDataLength(std::string_view msg_fragment, size_t original_length,
                      size_t* offset_of_payload = nullptr);

absl::Cord SerializeToCord(GenericRequestResponse* in, bool avoid_copy);

std::vector<iovec> Cord2Iovectors(const absl::Cord& in);

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_PAYLOAD_H_
