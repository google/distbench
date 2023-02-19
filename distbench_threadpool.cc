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

#include <queue>
#include <thread>

#include "absl/synchronization/notification.h"
#include "distbench_thread_support.h"
#include "glog/logging.h"
#include "thpool.h"

namespace distbench {

namespace {

class SimpleThreadpool : public AbstractThreadpool {
 public:
  SimpleThreadpool(int nb_threads);
  virtual ~SimpleThreadpool();
  virtual void AddWork(std::function<void()> function);

 private:
  mutable absl::Mutex mutex_;
  absl::Notification shutdown_;
  std::vector<std::thread> threads_;
  std::queue<std::function<void()> > work_queue_;
};

SimpleThreadpool::SimpleThreadpool(int nb_threads) {
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

SimpleThreadpool::~SimpleThreadpool() {
  shutdown_.Notify();
  for (auto& thread : threads_) {
    thread.join();
  }
}

void SimpleThreadpool::AddWork(std::function<void()> function) {
  absl::MutexLock m(&mutex_);
  work_queue_.push(function);
}

// This is the C-Threadpool from an external library.
// https://github.com/Pithikos/C-Thread-Pool
class CThreadpool : public AbstractThreadpool {
 public:
  CThreadpool(int nb_threads);
  virtual ~CThreadpool();
  virtual void AddWork(std::function<void()> function);

 private:
  static void Trampoline(void* function);
  threadpool thpool_;
};

CThreadpool::CThreadpool(int nb_threads) { thpool_ = thpool_init(nb_threads); }

CThreadpool::~CThreadpool() {
  thpool_wait(thpool_);
  thpool_destroy(thpool_);
}

void CThreadpool::AddWork(std::function<void()> function) {
  // Copy the functor object to the heap, and pass the address of the heap
  // object to the thread pool:
  auto* fpointer = new std::function<void()>(function);
  thpool_add_work(thpool_, Trampoline, fpointer);
}

void CThreadpool::Trampoline(void* function) {
  // Execute and delete the heap allocated copy of the functor object:
  auto f = reinterpret_cast<std::function<void()>*>(function);
  (*f)();
  delete f;
}

}  // anonymous namespace

std::unique_ptr<AbstractThreadpool> CreateThreadpool(std::string_view name,
                                                     int size) {
  if (name.empty() || name == "simple") {
    return std::make_unique<SimpleThreadpool>(size);
  } else if (name == "cthread") {
    return std::make_unique<CThreadpool>(size);
  } else {
    return {};
  }
}

}  // namespace distbench
