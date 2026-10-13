#include "alarm_state.h"

namespace esphome {
namespace voice_alarm {

void AlarmStateMachine::init() {
  state_ = AlarmState::IDLE;
}

void AlarmStateMachine::tick(std::time_t now) {
  switch (state_) {
    case AlarmState::IDLE:
      // Ready for new alarms
      break;

    case AlarmState::SET:
      // Waiting for alarm time — check every 1 second
      check_alarm_firing(now);
      break;

    case AlarmState::FIRING:
      // Alarm is ringing — handle subtype behavior
      handle_alarms();
      break;

    case AlarmState::DONE:
      transition_to(AlarmState::IDLE);
      break;
  }
}

void AlarmStateMachine::set_alarm(uint8_t hour, uint8_t minute, uint8_t days_mask) {
  alarm_hour_ = hour;
  alarm_minute_ = minute;
  alarm_days_mask_ = days_mask;
  alarm_enabled_ = true;

  if (state_ == AlarmState::IDLE || state_ == AlarmState::SET) {
    transition_to(AlarmState::SET);
  }

  // Save to NVS for persistence (placeholder — actual NVS save is separate)
}

void AlarmStateMachine::clear_alarm() {
  alarm_enabled_ = false;

  if (state_ == AlarmState::FIRING) {
    transition_to(AlarmState::DONE);
  } else {
    transition_to(AlarmState::IDLE);
  }
}

void AlarmStateMachine::stop() {
  if (state_ == AlarmState::FIRING) {
    transition_to(AlarmState::DONE);
  }
}

void AlarmStateMachine::transition_to(AlarmState new_state) {
  if (new_state != state_) {
    state_ = new_state;
  }
}

void AlarmStateMachine::check_alarm_firing(std::time_t now) {
  if (!alarm_enabled_) {
    transition_to(AlarmState::IDLE);
    return;
  }

  const std::tm *timeinfo = std::localtime(&now);

  if (timeinfo == nullptr) return;

  // Check if current time matches alarm time
  if (timeinfo->tm_hour == alarm_hour_ &&
      timeinfo->tm_min == alarm_minute_ &&
      timeinfo->tm_sec == 0) {

    // Check day mask
    uint8_t current_day = (timeinfo->tm_wday + 6) % 7;  // Convert Sun=0 to Mon=0
    if (alarm_days_mask_ & (1 << current_day)) {
      subtype_ = FiringSubtype::ALARM;
      transition_to(AlarmState::FIRING);
    }
  }
}

void AlarmStateMachine::handle_alarms() {
  // Subtype-specific behavior
  // ALARM: interruptible by wake-word or button
  // REMINDER: plays TTS to completion

  // Placeholder: actual audio/TTS handling is in separate component
}

}  // namespace voice_alarm
}  // namespace esphome
