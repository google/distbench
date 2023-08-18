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

#include "distbench_thread_support.h"

#include <memory>

#include "absl/base/const_init.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"

namespace distbench {

namespace {

ABSL_CONST_INIT absl::Mutex abort_mutex(absl::kConstInit);
int abort_max_threads = 0;
int abort_current_threads = 0;
std::function<void()> abort_callback;

}  // namespace

void SetOverloadAbortThreshhold(int max_threads) {
  absl::MutexLock m(&abort_mutex);
  abort_max_threads = max_threads;
  if (max_threads == 0) {
    abort_callback = nullptr;
  }
}

void SetOverloadAbortCallback(std::function<void()> callback) {
  absl::MutexLock m(&abort_mutex);
  abort_callback = std::move(callback);
}

std::thread RunRegisteredThread(std::string_view thread_name,
                                std::function<void()> f) {
  std::shared_ptr<absl::Notification> abort_callback_notification;
  absl::Notification abort_callback_started;
  {
    absl::MutexLock m(&abort_mutex);
    ++abort_current_threads;
    if (abort_current_threads > abort_max_threads) {
      if (abort_max_threads && abort_callback) {
        abort_callback_notification = std::make_shared<absl::Notification>();
        abort_max_threads = 0;
      }
    }
  }
  auto ret = std::thread([=, &abort_callback_started]() {
    RegisterThread(thread_name);
    if (abort_callback_notification) {
      abort_callback_started.Notify();
      if (abort_callback) {
        abort_callback();
      }
      abort_callback_notification->Notify();
    }
    f();
    absl::MutexLock m(&abort_mutex);
    --abort_current_threads;
  });
  if (abort_callback_notification) {
    // If we are going to signal an abort it happens inside the new thread.
    // We must wait for that thread to be up and running at least, but we don't
    // actually need to wait for the abort callback to return, as this may
    // result in deadlock. So We wait for 5 second and continue;
    abort_callback_started.WaitForNotification();
    abort_callback_notification->WaitForNotificationWithTimeout(
        absl::Milliseconds(5000));
  }
  return ret;
}

void RegisterThread(std::string_view thread_name) {
  // Pay no attention to the man behind the curtain.....
}

}  // namespace distbench
