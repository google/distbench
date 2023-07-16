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
#ifndef SIMPLE_CLOCK_H_
#define SIMPLE_CLOCK_H_

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace distbench {

class SimpleClock {
 public:
  virtual ~SimpleClock() = default;
  virtual absl::Time Now() = 0;
  virtual int64_t SetOffset(int64_t ns) { return ns; }
  virtual void SleepFor(absl::Duration duration) = 0;
  virtual bool MutexLockWhenWithDeadline(absl::Mutex* mu,
                                         const absl::Condition& condition,
                                         absl::Time deadline)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) = 0;
  virtual bool MutexAwaitWithDeadline(absl::Mutex* mu,
                                      const absl::Condition& condition,
                                      absl::Time deadline) = 0;
};

}  // namespace distbench

#endif  // SIMPLE_CLOCK_H_
