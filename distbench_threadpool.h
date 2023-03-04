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
#include <string_view>

namespace distbench {

class AbstractThreadpool {
 public:
  virtual ~AbstractThreadpool() = default;
  virtual void AddWork(std::function<void()> function) = 0;
};

std::unique_ptr<AbstractThreadpool> CreateThreadpool(
    std::string_view threadpool_type, int size);

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_THREADPOOL_H_
