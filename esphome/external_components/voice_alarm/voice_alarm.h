#pragma once

#include "esphome.h"
#include "alarm_config.h"

#ifdef USE_ESP32

namespace esphome {
namespace voice_alarm {

// Forward declarations for state machine
class AlarmStateMachine;
class VoiceAlarmClock;

class VoiceAlarmClock : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_priority() override { return 140.0f; }

  // GPIO pin configuration (overridable per device)
  void set_gpio_config(uint8_t speaker_pin, uint8_t button_pin, uint8_t rtc_irq_pin = 0) {
    this->speaker_pin_ = speaker_pin;
    this->button_pin_ = button_pin;
    this->rtc_irq_pin_ = rtc_irq_pin;
  }

  // State machine access
  void set_state_machine(AlarmStateMachine *state_machine) {
    this->state_machine_ = state_machine;
  }

  // RTC interface
  bool has_rtc_ = false;
  uint32_t last_sync_epoch_ = 0;

 protected:
  uint8_t speaker_pin_;
  uint8_t button_pin_;
  uint8_t rtc_irq_pin_;

  AlarmStateMachine *state_machine_ = nullptr;
};

}  // namespace voice_alarm
}  // namespace esphome

#endif