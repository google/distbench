// Copyright 2022 Google LLC
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

#include <queue>
#include <thread>
#include <vector>

#include "absl/synchronization/notification.h"
#include "distbench_utils.h"
#include "thpool.h"

namespace distbench {

class DistbenchThreadpool {
 public:
  DistbenchThreadpool(int nb_threads);
  ~DistbenchThreadpool();
  void AddWork(std::function<void()> function);

 private:
  mutable absl::Mutex mutex_;
  absl::Notification shutdown_;
  std::vector<std::thread> threads_;
  std::queue<std::function<void()> > work_queue_;

  // This is the C-Threadpool from an external library.
  // https://github.com/Pithikos/C-Thread-Pool
  threadpool thpool_;
};

}  // namespace distbench

#endif  // DISTBENCH_DISTBENCH_THREADPOOL_H_
