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

#include "protocol_driver.h"

#include "glog/logging.h"

namespace distbench {

void ServerRpcState::SetSendResponseFunction(
    std::function<void(void)> send_response_function) {
  send_response_function_ = send_response_function;
}

void ServerRpcState::SendResponseIfSet() const {
  if (send_response_function_) {
    send_response_function_();
  }
}

void ServerRpcState::SetFreeStateFunction(
    std::function<void(void)> free_state_function) {
  free_state_function_ = free_state_function;
}

void ServerRpcState::FreeStateIfSet() const {
  if (free_state_function_) {
    free_state_function_();
  }
}

class RealClock : public SimpleClock {
 public:
  ~RealClock() override {}
  absl::Time Now() override { return absl::Now(); }

  bool MutexLockWhenWithDeadline(absl::Mutex* mu,
                                 const absl::Condition& condition,
                                 absl::Time deadline)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) override {
    return mu->LockWhenWithDeadline(condition, deadline);
  }
};

SimpleClock& ProtocolDriver::GetClock() {
  static RealClock real_clock;
  return real_clock;
}

absl::StatusOr<std::string> ProtocolDriverClient::Preconnect() { return ""; }

void ProtocolDriverServer::HandleConnectFailure(
    std::string_view local_connection_info) {}

}  // namespace distbench
