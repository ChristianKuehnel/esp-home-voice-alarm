#include "alarm_request.h"

#include "alarm_calendar.h"

namespace esphome {
namespace voice_alarm {

RequestResult process_set_alarm_request(const SetAlarmRequest &request, uint32_t current_revision) {
  RequestStatus status = RequestStatus::OK;
  if (request.hour > 23) {
    status = RequestStatus::INVALID_HOUR;
  } else if (request.minute > 59) {
    status = RequestStatus::INVALID_MINUTE;
  } else if (request.days_mask > 127) {
    status = RequestStatus::INVALID_DAYS_MASK;
  } else if (request.days_mask == 0 && !valid_calendar_date(request.year, request.month, request.day)) {
    // Mask 0 is a one-shot alarm; a valid calendar date is what makes it one.
    status = RequestStatus::INVALID_DATE;
  } else if (request.expected_revision != current_revision) {
    status = RequestStatus::STALE_REVISION;
  }

  uint32_t revision = current_revision;
  if (status == RequestStatus::OK) {
    ++revision;
  }
  return RequestResult{status, request.request_id, revision};
}

const char *request_status_name(RequestStatus status) {
  switch (status) {
    case RequestStatus::OK:
      return "ok";
    case RequestStatus::STALE_REVISION:
      return "stale_revision";
    case RequestStatus::INVALID_HOUR:
      return "invalid_hour";
    case RequestStatus::INVALID_MINUTE:
      return "invalid_minute";
    case RequestStatus::INVALID_DAYS_MASK:
      return "invalid_days_mask";
    case RequestStatus::STORAGE_ERROR:
      return "storage_error";
    case RequestStatus::INVALID_DATE:
      return "invalid_date";
    case RequestStatus::PAST_ONE_SHOT:
      return "past_one_shot";
    case RequestStatus::NONEXISTENT_TIME:
      return "nonexistent_time";
  }
  return "unknown";
}

}  // namespace voice_alarm
}  // namespace esphome
