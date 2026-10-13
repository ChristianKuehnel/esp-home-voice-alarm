#pragma once

#include "alarm_state.h"
#include "esphome/core/component.h"
#include "esphome/components/time/real_time_clock.h"

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

 protected:
  time::RealTimeClock *clock_{nullptr};
  AlarmStateMachine state_machine_;
};

}  // namespace voice_alarm
}  // namespace esphome
