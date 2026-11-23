#pragma once

#include <cstdint>

namespace esphome {
namespace voice_alarm {

// Pure calendar helpers shared by the scheduler (alarm_state.cpp) and the
// request processor (alarm_request.cpp). Year is restricted to the range the
// stored schedule can represent.

inline bool is_leap(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

inline int days_in_month(uint16_t year, uint8_t month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12)
    return 0;
  if (month == 2 && is_leap(year))
    return 29;
  return kDays[month - 1];
}

inline bool valid_calendar_date(uint16_t year, uint8_t month, uint8_t day) {
  if (year < 1971 || year > 2100)
    return false;
  return day >= 1 && day <= static_cast<uint8_t>(days_in_month(year, month));
}

}  // namespace voice_alarm
}  // namespace esphome
