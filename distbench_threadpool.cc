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

#ifdef WITH_MERCURY
#include <mercury_thread_pool.h>
#endif

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

#ifdef WITH_MERCURY
class MercuryThreadpool : public AbstractThreadpool {
 public:
  MercuryThreadpool(int nb_threads);
  virtual ~MercuryThreadpool();
  virtual void AddWork(std::function<void()> function);

 private:
  struct heaper {
    struct hg_thread_work work_item; /* Must be first! */
    std::function<void()> function;
  };
  static void* Trampoline(void* function);
  std::unique_ptr<hg_thread_pool_t> thread_pool_;
};

MercuryThreadpool::MercuryThreadpool(int nb_threads) {
  hg_thread_pool_t* tmp;
  int error = hg_thread_pool_init(nb_threads, &tmp);
  thread_pool_.reset(tmp);
  if (error) {
    LOG(INFO) << "some kinda error: " << error;
  }
}

MercuryThreadpool::~MercuryThreadpool() {
  hg_thread_pool_destroy(thread_pool_.release());
}

void MercuryThreadpool::AddWork(std::function<void()> function) {
  // Copy the functor object to the heap, and pass the address of the heap
  // object to the thread pool:
  auto* hpointer = new heaper;
  hpointer->work_item.func = Trampoline;
  hpointer->work_item.args = hpointer;
  hpointer->function = function;
  hg_thread_pool_post(thread_pool_.get(), &hpointer->work_item);
}

void* MercuryThreadpool::Trampoline(void* heap_object) {
  // Execute and delete the heap allocated copy of the functor object:
  auto h = reinterpret_cast<heaper*>(heap_object);
  h->function();
  delete h;
  return 0;
}
#endif  // WITH_MERCURY
}  // anonymous namespace

std::unique_ptr<AbstractThreadpool> CreateThreadpool(std::string_view name,
                                                     int size) {
  if (name.empty() || name == "simple") {
    return std::make_unique<SimpleThreadpool>(size);
  } else if (name == "cthread") {
    return std::make_unique<CThreadpool>(size);
#ifdef WITH_MERCURY
  } else if (name == "mercury") {
    return std::make_unique<MercuryThreadpool>(size);
#endif
  } else {
    return {};
  }
}

}  // namespace distbench
