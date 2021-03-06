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

#include "distbench_threadpool.h"

namespace distbench {

DistbenchThreadpool::DistbenchThreadpool(int nb_threads) {
  auto task_runner = [&]() {
    do {
      std::function<void()> task;
      {
        absl::MutexLock m(&mutex_);
        if (work_queue_.empty()) {
          if (shutdown_.HasBeenNotified()) return;
          continue;
        }
        task = work_queue_.front();
        work_queue_.pop();
      }
      task();
    } while (true);
  };

  for (int i = 0; i < nb_threads; i++) {
    threads_.push_back(RunRegisteredThread("ThreadPool", task_runner));
  }
}

DistbenchThreadpool::~DistbenchThreadpool() {
  shutdown_.Notify();
  for (auto& thread : threads_) {
    thread.join();
  }
}

void DistbenchThreadpool::AddWork(std::function<void()> function) {
  absl::MutexLock m(&mutex_);
  work_queue_.push(function);
}

}  // namespace distbench
