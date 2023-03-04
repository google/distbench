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

#ifdef WITH_MERCURY
#include <mercury_thread_pool.h>
#endif

namespace distbench {

namespace {

class NullThreadpool : public AbstractThreadpool {
 public:
  NullThreadpool(int nb_threads);
  ~NullThreadpool() override;
  void AddWork(std::function<void()> task) override;
  std::vector<ThreadpoolStat> GetStats() override;

 private:
  mutable absl::Mutex mutex_;
  int active_threads_ = 0;
};

NullThreadpool::NullThreadpool(int nb_threads) {}

NullThreadpool::~NullThreadpool() {
  auto all_threads_done = [this]() { return active_threads_ == 0; };
  absl::MutexLock m(&mutex_);
  mutex_.Await(absl::Condition(&all_threads_done));
}

void NullThreadpool::AddWork(std::function<void()> task) {
  {
    absl::MutexLock m(&mutex_);
    ++active_threads_;
  }
  auto function_wrapper = [this, task = std::move(task)]() {
    task();
    absl::MutexLock m(&mutex_);
    --active_threads_;
  };
  RunRegisteredThread("NullThreadPool", function_wrapper).detach();
}

std::vector<ThreadpoolStat> NullThreadpool::GetStats() { return {}; }

class SimpleThreadpool : public AbstractThreadpool {
 public:
  SimpleThreadpool(int nb_threads);
  ~SimpleThreadpool() override;
  void AddWork(std::function<void()> task) override;
  std::vector<ThreadpoolStat> GetStats() override;

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
          task = std::move(work_queue_.front());
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

std::vector<ThreadpoolStat> SimpleThreadpool::GetStats() { return {}; }

void SimpleThreadpool::AddWork(std::function<void()> task) {
  absl::MutexLock m(&mutex_);
  work_queue_.push(std::move(task));
}

class ElasticThreadpool : public AbstractThreadpool {
 public:
  ElasticThreadpool(int nb_threads);
  ~ElasticThreadpool() override;
  void AddWork(std::function<void()> task) override;
  std::vector<ThreadpoolStat> GetStats() override;

 private:
  void TaskRunner(std::function<void()> task);
  bool WaitForTask() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_mutex_);

  const size_t max_idle_threads_ = 0;
  size_t work_count_ = 0;
  size_t idle_threads_ = 0;
  size_t threads_launched_ = 0;
  size_t active_threads_ = 0;
  mutable absl::Mutex work_mutex_;
  absl::Notification shutdown_;
  std::queue<std::function<void()> > work_queue_ ABSL_GUARDED_BY(work_mutex_);
};

ElasticThreadpool::ElasticThreadpool(int nb_threads)
    : max_idle_threads_(nb_threads) {}

void ElasticThreadpool::AddWork(std::function<void()> task) {
  auto ok_to_add = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_mutex_) {
    return (work_queue_.size() + 1) <= max_idle_threads_;
  };
  auto ok_conditions = absl::Condition(&ok_to_add);

  work_mutex_.Lock();
  work_mutex_.Await(ok_conditions);
  work_queue_.push(std::move(task));
  bool need_to_grow_threadpool =
      (idle_threads_ == 0) || (active_threads_ < max_idle_threads_ &&
                               work_queue_.size() > idle_threads_);
  if (need_to_grow_threadpool) {
    ++threads_launched_;
    ++active_threads_;
    task = std::move(work_queue_.front());
    work_queue_.pop();
  }
  work_mutex_.Unlock();
  if (need_to_grow_threadpool) {
    auto elastic_runner = [this, task = std::move(task)]() {
      TaskRunner(std::move(task));
    };
    RunRegisteredThread("ElasticThreadPool", elastic_runner).detach();
  }
}

bool ElasticThreadpool::WaitForTask() {
  auto work_available = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_mutex_) {
    return !work_queue_.empty() || shutdown_.HasBeenNotified();
  };
  auto working_conditions = absl::Condition(&work_available);
  idle_threads_++;
  absl::Duration timeout = (idle_threads_ > max_idle_threads_)
                               ? absl::Seconds(1)
                               : absl::InfiniteDuration();
  bool ret = work_mutex_.AwaitWithTimeout(working_conditions, timeout);
  idle_threads_--;
  return ret;
}

void ElasticThreadpool::TaskRunner(std::function<void()> task) {
  bool need_to_grow_threadpool = false;
  std::function<void()> task2;
  bool did_work = false;
  while (1) {
    if (task) {
      task();
      task = nullptr;
      did_work = true;
    }
    work_mutex_.Lock();
    if (did_work) {
      ++work_count_;
      did_work = false;
    }
    if (WaitForTask()) {
      // A task is available, or we are shutting down:
      if (work_queue_.empty()) {
        // The queue is empty and we are shutting down:
        --active_threads_;
        work_mutex_.Unlock();
        return;
      }
      // A task is available....
      task = std::move(work_queue_.front());
      work_queue_.pop();
      need_to_grow_threadpool = (idle_threads_ == 0 && !work_queue_.empty());
      if (need_to_grow_threadpool) {
        ++threads_launched_;
        ++active_threads_;
        task2 = std::move(work_queue_.front());
        work_queue_.pop();
      }
      work_mutex_.Unlock();
      if (need_to_grow_threadpool) {
        auto elastic_runner = [this, task2 = std::move(task2)]() {
          TaskRunner(std::move(task2));
        };
        RunRegisteredThread("ElasticThreadPool", elastic_runner).detach();
      }
    } else {
      // Timed out waiting for a task; maybe need to retire this thread:
      bool need_to_shrink_threadpool = idle_threads_ >= max_idle_threads_;
      if (need_to_shrink_threadpool) {
        --active_threads_;
      }
      work_mutex_.Unlock();
      if (need_to_shrink_threadpool) {
        return;
      }
    }
  }
}

std::vector<ThreadpoolStat> ElasticThreadpool::GetStats() {
  std::vector<ThreadpoolStat> ret;
  ret.resize(2);
  absl::MutexLock m(&work_mutex_);
  ret[0].name = "threads_launched";
  ret[0].value = threads_launched_;
  ret[1].name = "tasks_processed";
  ret[1].value = work_count_;
  return ret;
}

ElasticThreadpool::~ElasticThreadpool() {
  shutdown_.Notify();
  auto all_threads_done = [this]() { return active_threads_ == 0; };
  absl::MutexLock m(&work_mutex_);
  work_mutex_.Await(absl::Condition(&all_threads_done));
  if (0) {
    LOG(ERROR) << "we launched " << threads_launched_ << " threads";
    LOG(ERROR) << "we worked " << work_count_ << " times";
  }
}

#ifdef WITH_MERCURY
class MercuryThreadpool : public AbstractThreadpool {
 public:
  MercuryThreadpool(int nb_threads);
  ~MercuryThreadpool() override;
  void AddWork(std::function<void()> task) override;
  std::vector<ThreadpoolStat> GetStats() override;

 private:
  struct HeapObject {
    struct hg_thread_work work_item; /* Must be first! */
    std::function<void()> task;
  };
  static void* Trampoline(void* heap_object_pointer);
  std::unique_ptr<hg_thread_pool_t> thread_pool_;
};

MercuryThreadpool::MercuryThreadpool(int nb_threads) {
  hg_thread_pool_t* tmp;
  int error = hg_thread_pool_init(nb_threads, &tmp);
  thread_pool_.reset(tmp);
  if (error) {
    LOG(ERROR) << "some kinda error: " << error;
  }
}

MercuryThreadpool::~MercuryThreadpool() {
  hg_thread_pool_destroy(thread_pool_.release());
}

void MercuryThreadpool::AddWork(std::function<void()> task) {
  // Copy the functor object to the heap, and pass the address of the heap
  // object to the thread pool:
  auto* heap_object = new HeapObject;
  heap_object->work_item.func = Trampoline;
  heap_object->work_item.args = heap_object;
  heap_object->task = std::move(task);
  hg_thread_pool_post(thread_pool_.get(), &heap_object->work_item);
}

void* MercuryThreadpool::Trampoline(void* heap_object_pointer) {
  // Execute and delete the heap allocated copy of the functor object:
  auto heap_object = reinterpret_cast<HeapObject*>(heap_object_pointer);
  heap_object->task();
  delete heap_object;
  return 0;
}

std::vector<ThreadpoolStat> MercuryThreadpool::GetStats() { return {}; }
#endif  // WITH_MERCURY

}  // anonymous namespace

std::unique_ptr<AbstractThreadpool> CreateThreadpool(std::string_view name,
                                                     int size) {
  if (name == "simple") {
    return std::make_unique<SimpleThreadpool>(size);
  } else if (name.empty() || name == "elastic") {
    return std::make_unique<ElasticThreadpool>(size);
  } else if (name == "null") {
    return std::make_unique<NullThreadpool>(size);
#ifdef WITH_MERCURY
  } else if (name == "mercury") {
    return std::make_unique<MercuryThreadpool>(size);
#endif
  } else {
    return {};
  }
}

}  // namespace distbench
