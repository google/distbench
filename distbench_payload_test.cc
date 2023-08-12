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

#include "glog/logging.h"
#include "gtest/gtest.h"
#include "gtest_utils.h"

namespace distbench {

TEST(GetPaddingForSerializedSize, LargeRangesExact) {
  GenericRequestResponse request;
  // Interesting things happen near powers of 128, so we test near
  // 128, 128^2, 128^3, 128^4
  for (int i = 1; i < 5; ++i) {
    int size = 1 << (7 * i);
    // We test a range of values in the vicinity of each power of 128:
    for (int j = -16; j < 16; ++j) {
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
    for (int j = -16; j < 16; ++j) {
      size_t padding = GetPaddingForSerializedSize(&request, size + j);
      LOG(INFO) << "padding is " << padding;
      if (padding) request.set_payload(std::move(std::string(padding, 'D')));
      ASSERT_EQ(request.ByteSizeLong(), size + j);
      std::string bytes = request.SerializeAsString();
      LOG(INFO) << "trying for " << size + j;
      size_t mdl = MetaDataLength(bytes.substr(0, 4096), bytes.length());
      LOG(INFO) << "mdl = " << mdl;
      bytes.resize(mdl);
      GenericRequestResponse request2;
      ASSERT_TRUE(request2.ParseFromString(bytes));
      request2.clear_payload();
      LOG(INFO) << request2.DebugString();
    }
  }
}

}  // namespace distbench
