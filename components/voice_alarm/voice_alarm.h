#pragma once

#include <string>

#include "alarm_request.h"
#include "alarm_state.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace voice_alarm {

// Hardware callbacks live in the device recipe. Both targets share this core.
class VoiceAlarmClock : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  void set_time_source(time::RealTimeClock *clock) { clock_ = clock; }
  AlarmStateMachine &state_machine() { return state_machine_; }

  uint32_t current_revision() const { return revision_; }
  const RequestResult &last_result() const { return last_result_; }
  std::string last_error_message() const;

  // Runs the request through the pure processor, applies accepted requests to the
  // state machine, and commits the new revision to durable storage before
  // returning. Rejected requests change neither the schedule nor the revision.
  RequestResult process_set_alarm_request(uint32_t request_id, uint32_t expected_revision,
                                          uint8_t hour, uint8_t minute, uint8_t days_mask);

  // Key=value snapshot for HA refresh, e.g.
  // "revision=1;hour=7;minute=0;days_mask=31;enabled=1".
  std::string snapshot() const;

 protected:
  time::RealTimeClock *clock_{nullptr};
  AlarmStateMachine state_machine_;
  ESPPreferenceObject revision_preference_;
  uint32_t revision_{0};
  RequestResult last_result_{};
};

}  // namespace voice_alarm
}  // namespace esphome
