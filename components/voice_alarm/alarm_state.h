#pragma once

#include "esphome.h"

#ifdef USE_ESP32

namespace esphome {
namespace voice_alarm {

// State machine states
enum class AlarmState {
  IDLE,
  SET,
  FIRING,
  DONE
};

// FIRING subtype
enum class FiringSubtype {
  ALARM,
  REMINDER
};

class AlarmStateMachine {
 public:
  void init();
  void tick();

  AlarmState get_state() const { return state_; }
  FiringSubtype get_subtype() const { return subtype_; }

  void set_alarm(uint8_t hour, uint8_t minute, uint8_t days_mask);
  void clear_alarm();
  void stop();

 protected:
  void transition_to(AlarmState new_state);
  void check_alarm_firing();
  void handle_alarms();

  AlarmState state_ = AlarmState::IDLE;
  FiringSubtype subtype_ = FiringSubtype::ALARM;

  uint8_t alarm_hour_ = 0;
  uint8_t alarm_minute_ = 0;
  uint8_t alarm_days_mask_ = 0;
  bool alarm_enabled_ = false;
};

}  // namespace voice_alarm
}  // namespace esphome

#endif