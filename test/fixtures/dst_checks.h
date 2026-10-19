#pragma once

#include "esphome/components/voice_alarm/alarm_state.h"

// DST-specific cases. The caller must set TZ to a real DST zone (the native
// runner uses America/New_York) and call tzset() before invoking this. The
// host and Xtensa targets do not guarantee DST zone data, so this runs only in
// the native layer.
inline const char *dst_checks() {
  using esphome::voice_alarm::AlarmState;
  using esphome::voice_alarm::AlarmStateMachine;

  auto local = [](int year, int month, int day, int hour, int minute, int isdst) -> std::time_t {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = isdst;
    return mktime(&t);
  };

  // Spring 2026: America/New_York advances at 02:00 on 2026-03-08, so 02:30 on
  // that date does not exist. A recurring 02:30 skips the gap date without a
  // substitute 03:00 firing.
  {
    const std::time_t sat = local(2026, 3, 7, 2, 30, 0);  // 02:30 EST, exists.
    const std::time_t mon = local(2026, 3, 9, 2, 30, 1);  // 02:30 EDT, exists.
    const std::time_t gap_noon = local(2026, 3, 8, 12, 0, 0);
    const std::time_t gap_0300 = local(2026, 3, 8, 3, 0, 1);
    AlarmStateMachine m;
    m.init();
    m.add_repeating(2, 30, 127, sat - 3600);
    m.tick(sat, 1000);  // Sat 02:30 EST: fire.
    if (m.get_state() != AlarmState::RINGING) return "spring: did not fire before the gap";
    m.stop();
    m.tick(gap_noon, 2000);
    if (m.get_state() == AlarmState::RINGING) return "spring: fired on the gap date";
    m.tick(gap_0300, 2001);
    if (m.get_state() == AlarmState::RINGING) return "spring: substitute 03:00 fired";
    m.tick(mon, 3000);  // Mon 02:30 EDT: fire.
    if (m.get_state() != AlarmState::RINGING) return "spring: did not fire after the gap";
    m.stop();
  }

  // Autumn 2026: America/New_York falls back at 02:00 on 2026-11-01, so 01:30
  // occurs twice. A recurring 01:30 rings at most once across the repeated
  // hour (the reboot variant is proven by the M2.4 restart tests).
  {
    const std::time_t first = local(2026, 11, 1, 1, 30, 1);   // 01:30 EDT (first).
    const std::time_t second = local(2026, 11, 1, 1, 30, 0);  // 01:30 EST (second).
    if (second <= first) return "autumn: test setup invalid";
    const std::time_t next_day = local(2026, 11, 2, 1, 30, 0);  // 01:30 EST.
    AlarmStateMachine m;
    m.init();
    m.add_repeating(1, 30, 127, first - 3600);
    m.tick(first, 1000);  // first 01:30: fire.
    if (m.get_state() != AlarmState::RINGING) return "autumn: did not fire at the first occurrence";
    m.stop();
    m.tick(second, 2000);  // repeated 01:30: must not refire.
    if (m.get_state() == AlarmState::RINGING) return "autumn: refired in the repeated hour";
    m.tick(next_day, 3000);  // the next day still fires.
    if (m.get_state() != AlarmState::RINGING) return "autumn: next day did not fire";
    m.stop();
  }
  return nullptr;
}
