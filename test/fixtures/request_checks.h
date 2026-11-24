#pragma once

#include "esphome/components/voice_alarm/alarm_request.h"

// The same request/ack assertions run on native, ESPHome host, and Xtensa builds.
inline const char *request_checks() {
  using esphome::voice_alarm::RequestStatus;
  using esphome::voice_alarm::SetAlarmRequest;
  using esphome::voice_alarm::process_set_alarm_request;

  const SetAlarmRequest valid{0xA1, 0, 7, 0, 31};
  const auto ok = process_set_alarm_request(valid, 0);
  if (ok.status != RequestStatus::OK) return "valid request not accepted";
  if (ok.request_id != 0xA1) return "request id not echoed";
  if (ok.revision != 1) return "revision not advanced";

  const SetAlarmRequest stale{0xA2, 0, 7, 0, 31};
  const auto rejected = process_set_alarm_request(stale, 1);
  if (rejected.status != RequestStatus::STALE_REVISION) return "stale revision accepted";
  if (rejected.revision != 1) return "stale request changed revision";
  if (rejected.request_id != 0xA2) return "stale ack did not echo request id";

  const SetAlarmRequest bad_hour{0xA3, 1, 24, 0, 31};
  if (process_set_alarm_request(bad_hour, 1).status != RequestStatus::INVALID_HOUR) return "hour 24 accepted";
  const SetAlarmRequest bad_minute{0xA4, 1, 23, 60, 31};
  if (process_set_alarm_request(bad_minute, 1).status != RequestStatus::INVALID_MINUTE) return "minute 60 accepted";
  const SetAlarmRequest bad_mask{0xA5, 1, 23, 59, 128};
  if (process_set_alarm_request(bad_mask, 1).status != RequestStatus::INVALID_DAYS_MASK)
    return "days mask 128 accepted";

  const SetAlarmRequest boundary{0xA6, 1, 23, 59, 127};
  const auto advanced = process_set_alarm_request(boundary, 1);
  if (advanced.status != RequestStatus::OK || advanced.revision != 2) return "boundary values rejected";

  // --- One-shot requests (days_mask 0): the date fields complete the request.
  const SetAlarmRequest one_shot{0xA7, 1, 9, 6, 0, 2026, 10, 13};
  const auto shot_ok = process_set_alarm_request(one_shot, 1);
  if (shot_ok.status != RequestStatus::OK) return "valid one-shot rejected";
  if (shot_ok.revision != 2) return "one-shot did not advance the revision";
  const SetAlarmRequest shot_no_date{0xA8, 1, 9, 6, 0, 0, 0, 0};
  if (process_set_alarm_request(shot_no_date, 1).status != RequestStatus::INVALID_DATE)
    return "one-shot without a date accepted";
  const SetAlarmRequest shot_feb30{0xA9, 1, 9, 6, 0, 2026, 2, 30};
  if (process_set_alarm_request(shot_feb30, 1).status != RequestStatus::INVALID_DATE)
    return "one-shot with Feb 30 accepted";
  const SetAlarmRequest shot_year0{0xAA, 1, 9, 6, 0, 1970, 1, 1};
  if (process_set_alarm_request(shot_year0, 1).status != RequestStatus::INVALID_DATE)
    return "one-shot before 1971 accepted";
  // Field validation precedes the revision check for one-shots, like other fields.
  const SetAlarmRequest shot_stale{0xAC, 1, 9, 6, 0, 2026, 10, 13};
  const auto shot_stale_rejected = process_set_alarm_request(shot_stale, 5);
  if (shot_stale_rejected.status != RequestStatus::STALE_REVISION)
    return "one-shot with stale revision accepted";
  if (shot_stale_rejected.revision != 5) return "stale one-shot changed revision";
  // The date fields are ignored for a repeating alarm.
  const SetAlarmRequest repeating_with_date{0xAB, 1, 7, 0, 31, 2026, 2, 30};
  if (process_set_alarm_request(repeating_with_date, 1).status != RequestStatus::OK)
    return "repeating alarm rejected for its date fields";
  return nullptr;
}
