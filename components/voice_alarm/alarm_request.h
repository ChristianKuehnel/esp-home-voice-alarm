#pragma once

#include <cstdint>

namespace esphome {
namespace voice_alarm {

// Request/acknowledgment protocol version advertised in action responses.
// v2 added the one-shot date fields (year/month/day); days_mask 0 selects a
// one-shot at that explicit date, 1..127 a repeating weekday mask.
constexpr uint32_t kRequestSchemaVersion = 2;

enum class RequestStatus : uint8_t {
  OK = 0,
  STALE_REVISION = 1,
  INVALID_HOUR = 2,
  INVALID_MINUTE = 3,
  INVALID_DAYS_MASK = 4,
  STORAGE_ERROR = 5,
  INVALID_DATE = 6,
  PAST_ONE_SHOT = 7,
  NONEXISTENT_TIME = 8,
};

struct SetAlarmRequest {
  uint32_t request_id;
  uint32_t expected_revision;
  uint8_t hour;
  uint8_t minute;
  uint8_t days_mask;  // 0 = one-shot at year/month/day; 1..127 = weekday mask.
  uint16_t year{0};   // One-shot only; ignored for repeating alarms.
  uint8_t month{0};
  uint8_t day{0};
};

struct RequestResult {
  RequestStatus status{RequestStatus::OK};
  uint32_t request_id{0};
  uint32_t revision{0};  // Authoritative revision after processing the request.
};

// Decides a set-alarm command without touching storage or the state machine.
// Fields are validated before the revision; stale or invalid requests leave the
// revision unchanged, accepted requests advance it by one, and the caller is
// responsible for committing the new revision to durable storage.
RequestResult process_set_alarm_request(const SetAlarmRequest &request, uint32_t current_revision);

// Stable wire strings used in action responses.
const char *request_status_name(RequestStatus status);

}  // namespace voice_alarm
}  // namespace esphome
