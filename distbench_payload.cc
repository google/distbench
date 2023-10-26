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

#include "distbench_payload.h"

#include <fcntl.h>
#include <sys/resource.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace distbench {

const int kPayloadField = 0xf;

bool ReadVarInt(const char** start, const char* end, int64_t* out) {
  if (*start > end) return false;
  int64_t ret = 0;
  const char* cursor = *start;
  int64_t bits = 0;
  uint64_t byte;
  do {
    byte = *cursor & 0xff;
    ++cursor;
    ret |= (byte & 0x7f) << bits;
    bits += 7;
  } while ((byte >= 128) && (bits < 64) && cursor <= end);

  if (cursor - *start > 10) {
    return false;
  }
  if (byte >= 128) {
    return false;
  }

  *start = cursor;
  *out = ret;
  return true;
}

int VarIntSize(uint64_t val) {
  int ret = 1;
  while (val >= 128) {
    val >>= 7;
    ret++;
  }
  return ret;
}

size_t MetaDataLength(std::string_view msg_fragment, size_t original_length,
                      size_t* offset_of_payload) {
  const char* cursor = msg_fragment.data();
  const char* end = cursor + msg_fragment.size() - 1;
  while (cursor <= end) {
    int64_t tag;
    const char* field_start = cursor;
    if (!ReadVarInt(&cursor, end, &tag)) {
      return original_length;
    }
    int64_t field_id = tag >> 3;
    int type = tag & 0x7;
    int64_t value;
    switch (type) {
      case 0:
        if (!ReadVarInt(&cursor, end, &value)) {
          return original_length;
        }
        break;

      case 2:
        if (!ReadVarInt(&cursor, end, &value)) {
          return original_length;
        }
        cursor += value;
        if (field_id == kPayloadField) {
          if (end <= cursor) {
            if (offset_of_payload) {
              *offset_of_payload = cursor - value - msg_fragment.data();
            }
            return field_start - msg_fragment.data();
          } else {
            return original_length;
          }
        }
        break;

      case 3:
      case 4:
        cursor += 0;
        break;

      case 1:
        cursor += 8;
        break;

      case 5:
        cursor += 4;
        break;

      default:
        return original_length;
    }
  }

  CHECK_EQ(cursor - end, 0)
      << msg_fragment.length() << " of " << original_length;

  return original_length;
}

size_t GetPaddingForSerializedSize(GenericRequestResponse* msg,
                                   size_t target_size) {
  if (target_size == msg->ByteSizeLong()) {
    return 0;
  }
  msg->clear_payload();
  msg->clear_trim();
  if (target_size <= msg->ByteSizeLong()) {
    return 0;
  }

  msg->set_payload(std::move(std::string()));
  size_t start_size = msg->ByteSizeLong();
  if (start_size >= target_size) {
    return 0;
  }

  ssize_t pad = target_size - start_size;
  ssize_t field_length_delta = VarIntSize(pad) - VarIntSize(0);

  if (field_length_delta > 0) {
    // This only happens when the payload length is > 128, so it is always
    // safe to shorten it. The problem is that if the length is just over
    // 128^N then shortening it may reduce the number of bytes in the length,
    // which would leave the proto 1 byte too short. Adding one byte will then
    // make it 1 byte too long again. To avoid oscilating around the desired
    // length we set the trim field, which adds 2 bytes to the encoding.
    ssize_t new_pad = pad - field_length_delta;

    // This should always be 0 or 1:
    ssize_t field_length_delta2 = VarIntSize(pad) - VarIntSize(new_pad);

    if (field_length_delta2 > 0) {
      new_pad += field_length_delta2;
      msg->set_trim(false);
      new_pad -= 2;
    }
    pad = new_pad;
  }
  return pad;
}

absl::Cord PayloadAllocator::AllocPayloadCord(size_t size) {
  if (size > 0) {
    return absl::Cord(std::string(size, 'D'));
  }
  return absl::Cord();
}

void PayloadAllocator::AddPadding(GenericRequestResponse* msg,
                                  size_t target_size) {
  size_t padding = GetPaddingForSerializedSize(msg, target_size);
  if (padding > 0) {
    msg->set_payload(AllocPayloadCord(padding));
  }
}

std::string MakeVarInt(uint64_t val) {
  std::string ret(10, '\0');
  char* cursor = ret.data();
  do {
    if (val > 127) {
      *cursor = val | 0x80;
    } else {
      *cursor = val;
    }
    val >>= 7;
    ++cursor;
  } while (val);
  ret.resize(cursor - ret.data());
  return ret;
}

absl::Cord SerializeToCord(GenericRequestResponse* in, bool avoid_copy) {
  if (!avoid_copy || !in->has_payload()) {
    return absl::Cord(in->SerializeAsString());
  }
  GenericRequestResponse copy = *in;
  copy.clear_payload();
  // Insert metadata, followed by the field tag and length for the payload:
  const int kPayloadTag = (kPayloadField << 3) | 0x2;  // field 15, wire type 2
  absl::Cord ret(absl::StrCat(copy.SerializeAsString(), MakeVarInt(kPayloadTag),
                              MakeVarInt(in->payload().size())));
  ret.Append(in->payload());
  return ret;
}

std::vector<iovec> Cord2Iovectors(const absl::Cord& in) {
  std::vector<iovec> ret;

  int num_chunks = 0;
  for (ABSL_ATTRIBUTE_UNUSED const auto& chunk : in.Chunks()) {
    ++num_chunks;
  }

  ret.reserve(num_chunks);

  for (const auto& chunk : in.Chunks()) {
    iovec t = {const_cast<char*>(chunk.data()), chunk.length()};
    ret.push_back(t);
  }

  return ret;
}

}  // namespace distbench
