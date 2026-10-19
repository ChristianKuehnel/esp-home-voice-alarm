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
  return nullptr;
}
