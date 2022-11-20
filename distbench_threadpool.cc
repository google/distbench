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

#ifdef USE_DISTBENCH_THREADPOOL

DistbenchThreadpool::DistbenchThreadpool(int nb_threads) {
  for (int i = 0; i < nb_threads; i++) {
    auto task_runner = [&, i]() {
      do {
        std::function<void()> task;
        {
          absl::MutexLock m(&mutex_);
          if (work_queue_.empty()) {
            // All threads except thread-0 relinquish CPU
            // if there is no work.
            if (i != 0) sched_yield();
            if (shutdown_.HasBeenNotified()) return;
            continue;
          }
          task = work_queue_.front();
          work_queue_.pop();
        }
        task();
      } while (true);
    };
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

#else

DistbenchThreadpool::DistbenchThreadpool(int nb_threads) {
  thpool_ = thpool_init(nb_threads);
}

DistbenchThreadpool::~DistbenchThreadpool() {
  thpool_wait(thpool_);
  thpool_destroy(thpool_);
}

void Trampoline(void* function) {
  auto f = reinterpret_cast<std::function<void()>*>(function);
  (*f)();
  delete f;
}

void DistbenchThreadpool::AddWork(std::function<void()> function) {
  auto* fpointer = new std::function<void()>(function);
  thpool_add_work(thpool_, Trampoline, fpointer);
}

#endif

}  // namespace distbench
