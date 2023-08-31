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

#include "gtest/gtest.h"

namespace distbench {

TEST(GetPaddingForSerializedSize, LargeRangesExact) {
  GenericRequestResponse request;
  // Interesting things happen near powers of 128, so we test near
  // 128, 128^2, 128^3, 128^4
  for (int i = 1; i < 5; ++i) {
    int size = 1 << (7 * i);
    // We test a range of values in the vicinity of each power of 128:
    for (int j = -8; j < 8; ++j) {
      size_t padding = GetPaddingForSerializedSize(&request, size + j);
      if (padding) request.set_payload(std::move(std::string(padding, 'D')));
      ASSERT_EQ(request.ByteSizeLong(), size + j);
    }
  }
}

TEST(GetPaddingForSerializedSize, SmallRangeCloseEnough) {
  GenericRequestResponse request;
  for (int i = 0; i < 256; ++i) {
    size_t padding = GetPaddingForSerializedSize(&request, i);
    if (padding) request.set_payload(std::move(std::string(padding, 'D')));
    ASSERT_GE(request.ByteSizeLong(), i);
  }
}

TEST(MetaDataLength, First4K) {
  GenericRequestResponse request;
  request.set_rpc_index(0x11);
  request.mutable_trace_context();
  request.set_warmup(true);
  // request.set_after_payload32(1);
  // request.set_after_payload64(1);
  request.set_before_payload32(1);
  request.set_before_payload64(1);
  // Interesting things happen near powers of 128, so we test near
  // 128, 128^2, 128^3, 128^4
  for (int i = 1; i < 5; ++i) {
    int size = 1 << (7 * i);
    // We test a range of values in the vicinity of each power of 128:
    for (int j = -8; j < 8; ++j) {
      size_t padding = GetPaddingForSerializedSize(&request, size + j);
      if (padding) request.set_payload(std::move(std::string(padding, 'D')));
      ASSERT_EQ(request.ByteSizeLong(), size + j);
      std::string bytes = request.SerializeAsString();
      size_t mdl = MetaDataLength(bytes.substr(0, 4096), bytes.length());
      bytes.resize(mdl);
      GenericRequestResponse request2;
      ASSERT_TRUE(request2.ParseFromString(bytes));
      request2.clear_payload();
    }
  }
}

TEST(ReadVarInt, T) {
  std::set<uint64_t> interesting_nums = {
    0, 1, 127, 128, 16383, 16384, (1ULL << 21) -1, 1ULL << 21,
    (1ULL << 28) -1, 1ULL << 28, (1ULL << 35) -1, 1ULL << 35,
    (1ULL << 42) -1, 1ULL << 42, (1ULL << 49) -1, 1ULL << 49,
    (1ULL << 56) -1, 1ULL << 56, (1ULL << 63) -1, ~0ULL};

  for (uint64_t num : interesting_nums) {
    GenericRequestResponse t;
    t.set_response_payload_size(num);
    std::string rep = t.SerializeAsString();
    int64_t tag, val;
    ASSERT_FALSE(rep.empty());
    const char* start = rep.c_str();
    const char* end = rep.c_str() + rep.length() - 1;
    EXPECT_TRUE(ReadVarInt(&start, end, &tag));
    EXPECT_TRUE(ReadVarInt(&start, end, &val));
    ASSERT_FALSE(ReadVarInt(&start, end, &val));
    EXPECT_EQ(start, end + 1);
    EXPECT_EQ(val, num);
  }
}

TEST(MakeVarInt, T) {
  std::set<uint64_t> interesting_nums = {
    0, 1, 127, 128, 16383, 16384, (1ULL << 21) -1, 1ULL << 21,
    (1ULL << 28) -1, 1ULL << 28, (1ULL << 35) -1, 1ULL << 35,
    (1ULL << 42) -1, 1ULL << 42, (1ULL << 49) -1, 1ULL << 49,
    (1ULL << 56) -1, 1ULL << 56, (1ULL << 63) -1, ~0ULL};

  for (uint64_t num : interesting_nums) {
    std::string rep = MakeVarInt(num);
    ASSERT_FALSE(rep.empty());
    const char* start = rep.c_str();
    const char* end = start + rep.length() - 1;
    int64_t out = 90210;
    EXPECT_TRUE(ReadVarInt(&start, end, &out));
    EXPECT_EQ(out, num);
    EXPECT_EQ(start, end + 1);
  }
}

TEST(VarIntSize, T) {
  std::map<uint64_t, int> expected_sizes = {
    {0, 1}, {127, 1}, {128, 2}, {16383, 2}, {16384, 3},
    {(1ULL << 21) -1, 3}, {1ULL << 21, 4},
    {(1ULL << 28) -1, 4}, {1ULL << 28, 5},
    {(1ULL << 35) -1, 5}, {1ULL << 35, 6},
    {(1ULL << 42) -1, 6}, {1ULL << 42, 7},
    {(1ULL << 49) -1, 7}, {1ULL << 49, 8},
    {(1ULL << 56) -1, 8}, {1ULL << 56, 9},
    {(1ULL << 63) -1, 9}, {~0ULL, 10},
  };

  for (const auto& [v, size] : expected_sizes) {
    EXPECT_EQ(VarIntSize(v), size) << v;
    EXPECT_EQ(MakeVarInt(v).length(), size) << v;
  }
}

}  // namespace distbench
