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
#ifndef DISTBENCH_ACTIVITY_H_
#define DISTBENCH_ACTIVITY_H_

#include "absl/status/statusor.h"
#include "activity.h"
#include "distbench.pb.h"
#include "distbench_utils.h"
#include "glog/logging.h"
#include "traffic_config.pb.h"

namespace distbench {

enum class ActivityName {
  kWasteCpu = 0,
  kPolluteCache,
};

class Activity {
 public:
  virtual absl::Status Initialize(const ActivityConfig& ac) = 0;
  virtual void DoActivity() = 0;
  virtual ActivityLog GetActivityLog() = 0;
  std::string activity_name_;
  int iteration_count_ = 0;
};

class WasteCpu : public Activity {
 public:
  void DoActivity() override;
  ActivityLog GetActivityLog() override;
  absl::Status Initialize(const ActivityConfig& ac) override;

 private:
  int array_size_ = -1;
};

absl::StatusOr<std::unique_ptr<Activity>> AllocateActivity(
    const ActivityConfig& ac);

}  // namespace distbench

#endif  // ACTIVITY_H_
