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

#ifndef DISTBENCH_DISTBENCH_THREAD_SUPPORT_H_
#define DISTBENCH_DISTBENCH_THREAD_SUPPORT_H_

#include <functional>
#include <string_view>
#include <thread>

namespace distbench {

std::thread RunRegisteredThread(std::string_view thread_name,
                                std::function<void()> f);

void RegisterThread(std::string_view thread_name);

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_THREAD_SUPPORT_H_
