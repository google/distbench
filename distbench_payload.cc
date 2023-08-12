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

#include <cerrno>
#include <fstream>
#include <streambuf>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

namespace distbench {

namespace {

bool ReadVarInt(const char** start, const char* end, int64_t* out) {
  int64_t ret = 0;
  const char* cursor = *start;
  int64_t bits = 0;
  unsigned char byte;
  do {
    byte = *cursor;
    ++cursor;
    ret |= (byte & 0x7f) << bits;
    bits += 7;
  } while ((byte++ >= 128) && (bits < 64) && cursor < end);

  if (bits >= 64) return false;
  if (byte >= 128) return false;

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

}  // namespace

size_t MetaDataLength(std::string_view msg_fragment, size_t original_length) {
  const char* cursor = msg_fragment.data();
  const char* end = cursor + msg_fragment.size();
  while (cursor < end) {
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
        if (field_id == 0xf) {
          if (end <= cursor) {
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
    return absl::Cord(std::move(std::string(size, 'D')));
  }
  return absl::Cord(std::move(std::string(0, 'D')));
}

void PayloadAllocator::AddPadding(GenericRequestResponse* msg,
                                  size_t target_size) {
  size_t padding = GetPaddingForSerializedSize(msg, target_size);
  if (padding > 0) {
    msg->set_payload(AllocPayloadCord(padding));
  }
}

}  // namespace distbench
