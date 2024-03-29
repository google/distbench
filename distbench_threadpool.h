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

#ifndef DISTBENCH_DISTBENCH_THREADPOOL_H_
#define DISTBENCH_DISTBENCH_THREADPOOL_H_

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace distbench {

struct ThreadpoolStat {
  std::string name;
  int64_t value;
};

class AbstractThreadpool {
 public:
  virtual ~AbstractThreadpool() = default;
  virtual void AddTask(std::function<void()> task) = 0;
  virtual std::vector<ThreadpoolStat> GetStats() = 0;
};

absl::StatusOr<std::unique_ptr<AbstractThreadpool>> CreateThreadpool(
    std::string_view threadpool_type, int size);

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_THREADPOOL_H_
