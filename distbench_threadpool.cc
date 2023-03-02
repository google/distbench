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
  ~SimpleThreadpool() override;
  void AddWork(std::function<void()> function) override;

 private:
  mutable absl::Mutex mutex_;
  absl::Notification shutdown_;
  int active_threads_ = 0;
  std::queue<std::function<void()> > work_queue_;
};

SimpleThreadpool::SimpleThreadpool(int nb_threads) {
  active_threads_ = nb_threads;
  for (int i = 0; i < nb_threads; i++) {
    auto task_runner = [this]() {
      auto work_available = [this]() {
        return !work_queue_.empty() || shutdown_.HasBeenNotified();
      };
      auto working_conditions = absl::Condition(&work_available);
      do {
        std::function<void()> task;
        {
          absl::MutexLock m(&mutex_);
          mutex_.Await(working_conditions);
          if (work_queue_.empty()) {
            --active_threads_;
            return;
          }
          task = work_queue_.front();
          work_queue_.pop();
        }
        task();
      } while (true);
    };
    RunRegisteredThread("SimpleThreadPool", task_runner).detach();
  }
}

SimpleThreadpool::~SimpleThreadpool() {
  shutdown_.Notify();
  auto all_threads_done = [this]() { return active_threads_ == 0; };
  absl::MutexLock m(&mutex_);
  mutex_.Await(absl::Condition(&all_threads_done));
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
  static void Trampoline(void* heap_object_pointer);
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

void CThreadpool::Trampoline(void* heap_object_pointer) {
  // Execute and delete the heap allocated copy of the functor object:
  auto f = reinterpret_cast<std::function<void()>*>(heap_object_pointer);
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
  struct HeapObject {
    struct hg_thread_work work_item; /* Must be first! */
    std::function<void()> function;
  };
  static void* Trampoline(void* heap_object_pointer);
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
  auto* heap_object = new HeapObject;
  heap_object->work_item.func = Trampoline;
  heap_object->work_item.args = heap_object;
  heap_object->function = function;
  hg_thread_pool_post(thread_pool_.get(), &heap_object->work_item);
}

void* MercuryThreadpool::Trampoline(void* heap_object_pointer) {
  // Execute and delete the heap allocated copy of the functor object:
  auto heap_object = reinterpret_cast<HeapObject*>(heap_object_pointer);
  heap_object->function();
  delete heap_object;
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
