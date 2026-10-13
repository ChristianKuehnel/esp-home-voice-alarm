#pragma once

#include "esphome/components/voice_alarm/alarm_state.h"

// The same assertions run on native, ESPHome host, and Xtensa builds.
// Callers set TZ=UTC0. This covers the scaffold, not the complete v1 contract.
inline const char *scheduler_checks() {
  using esphome::voice_alarm::AlarmState;
  using esphome::voice_alarm::AlarmStateMachine;
  using esphome::voice_alarm::FiringSubtype;
  constexpr std::time_t monday = 1791788400;  // 2026-10-12 07:00:00 UTC.
  AlarmStateMachine alarm;
  alarm.init();
  alarm.tick(monday);
  if (alarm.get_state() != AlarmState::IDLE) return "empty scheduler fired";
  alarm.stop();
  if (alarm.get_state() != AlarmState::IDLE) return "idle stop changed state";
  alarm.set_alarm(7, 0, 1);
  alarm.tick(monday - 60);
  if (alarm.get_state() != AlarmState::SET) return "alarm fired early";
  alarm.tick(monday + 86400);
  if (alarm.get_state() != AlarmState::SET) return "wrong weekday fired";
  alarm.tick(monday);
  if (alarm.get_state() != AlarmState::FIRING) return "Monday did not fire";
  if (alarm.get_subtype() != FiringSubtype::ALARM) return "incorrect firing type";
  alarm.stop();
  if (alarm.get_state() == AlarmState::FIRING) return "stop left alarm ringing";

  for (unsigned day = 0; day < 7; ++day) {
    AlarmStateMachine daily;
    daily.init();
    daily.set_alarm(7, 0, 1U << day);
    daily.tick(monday + day * 86400);
    if (daily.get_state() != AlarmState::FIRING) return "weekday mapping incorrect";
    daily.clear_alarm();
    daily.tick(monday + day * 86400);
    if (daily.get_state() == AlarmState::FIRING) return "cleared alarm still ringing";
  }

  AlarmStateMachine cancelled;
  cancelled.init();
  cancelled.set_alarm(7, 0, 127);
  cancelled.clear_alarm();
  cancelled.tick(monday);
  if (cancelled.get_state() != AlarmState::IDLE) return "cancelled schedule fired";
  return nullptr;
}
