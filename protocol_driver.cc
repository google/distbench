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

#include <time.h>

#include "protocol_driver.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/time.h"

namespace distbench {

void ServerRpcState::SetSendResponseFunction(
    std::function<void(void)> send_response_function) {
  send_response_function_ = send_response_function;
}

void ServerRpcState::SendResponseIfSet() {
  if (send_response_function_) {
#ifdef NDEBUG
    send_response_function_();
#else
    auto send_func = std::move(send_response_function_);
    send_response_function_ = []() {
      LOG(FATAL) << "response was already sent";
    };
    send_func();
#endif
  }
}

void ServerRpcState::SetFreeStateFunction(
    std::function<void(void)> free_state_function) {
  free_state_function_ = free_state_function;
}

void ServerRpcState::FreeStateIfSet() {
  if (free_state_function_) {
#ifdef NDEBUG
    free_state_function_();
#else
    auto free_func = std::move(free_state_function_);
    free_state_function_ = []() { LOG(FATAL) << "state was already freed"; };
    free_func();
#endif
  }
}

class RealClock : public SimpleClock {
 public:
  ~RealClock() override {}
  absl::Time Now() override { return absl::Now(); }

  void SleepFor(absl::Duration duration) override { absl::SleepFor(duration); }

  void SpinFor(absl::Duration duration) override {
    struct timespec tp;
    CHECK(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp));
    absl::Duration start = absl::DurationFromTimespec(tp);
    absl::Duration now;
    do {
      CHECK(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp));
      now = absl::DurationFromTimespec(tp);
    } while (now - start < duration);
  }

  bool MutexLockWhenWithDeadline(absl::Mutex* mu,
                                 const absl::Condition& condition,
                                 absl::Time deadline)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu) override {
    return mu->LockWhenWithDeadline(condition, deadline);
  }

  bool MutexAwaitWithDeadline(absl::Mutex* mu, const absl::Condition& condition,
                              absl::Time deadline) override {
    return mu->AwaitWithDeadline(condition, deadline);
  }
};

SimpleClock& ProtocolDriver::GetClock() {
  static RealClock real_clock;
  return real_clock;
}

absl::StatusOr<std::string> ProtocolDriverClient::Preconnect() { return ""; }

void ProtocolDriverServer::HandleConnectFailure(
    std::string_view local_connection_info) {}

void ProtocolDriverClient::SetNumMultiServerChannels(int num_channels) {}

absl::Status ProtocolDriverClient::SetupMultiServerChannel(
    const ::google::protobuf::RepeatedPtrField<NamedSetting>& settings,
    const std::vector<int>& peer_ids, int channel_id) {
  return absl::UnimplementedError(
      "This protocol driver does not support multi-server channels.");
}

void ProtocolDriverClient::InitiateRpcToMultiServerChannel(
    int channel_index, ClientRpcState* state,
    std::function<void(void)> done_callback) {
  state->end_time = absl::Now();
  state->success = 0;
  state->response.set_error_message(
      "This protocol driver does not support multi-server channels.");
  done_callback();
}

}  // namespace distbench
