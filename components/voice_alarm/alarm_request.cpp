#include "alarm_request.h"

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
  }
  return "unknown";
}

}  // namespace voice_alarm
}  // namespace esphome
