#include "voice_alarm.h"

#include <cstdio>

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace voice_alarm {

static const char *const TAG = "voice_alarm";

namespace {
constexpr uint32_t REVISION_PREF_MAGIC = 0x76617270;  // "varp", reserved for the clock revision.
}  // namespace

void VoiceAlarmClock::setup() {
  state_machine_.init();
  this->revision_preference_ = global_preferences->make_preference<uint32_t>(REVISION_PREF_MAGIC);
  this->revision_preference_.load(&this->revision_);
}

void VoiceAlarmClock::update() {
  if (clock_ == nullptr)
    return;
  const auto now = clock_->now();
  if (now.is_valid())
    state_machine_.tick(now.timestamp);
}

RequestResult VoiceAlarmClock::process_set_alarm_request(uint32_t request_id, uint32_t expected_revision,
                                                         uint8_t hour, uint8_t minute, uint8_t days_mask) {
  const SetAlarmRequest request{request_id, expected_revision, hour, minute, days_mask};
  const RequestResult result = esphome::voice_alarm::process_set_alarm_request(request, this->revision_);
  this->last_result_ = result;
  if (result.status == RequestStatus::OK) {
    this->state_machine_.set_alarm(hour, minute, days_mask);
    this->revision_ = result.revision;
    if (!this->revision_preference_.save(&this->revision_) || !global_preferences->sync()) {
      ESP_LOGE(TAG, "Failed to persist alarm revision");
    }
  }
  return result;
}

std::string VoiceAlarmClock::last_error_message() const {
  if (this->last_result_.status == RequestStatus::OK)
    return "";
  switch (this->last_result_.status) {
    case RequestStatus::STALE_REVISION:
      return "expected_revision does not match the device revision";
    case RequestStatus::INVALID_HOUR:
      return "hour must be 0..23";
    case RequestStatus::INVALID_MINUTE:
      return "minute must be 0..59";
    case RequestStatus::INVALID_DAYS_MASK:
      return "days_mask must be 0..127";
  }
  return "invalid request";
}

std::string VoiceAlarmClock::snapshot() const {
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "revision=%u;hour=%u;minute=%u;days_mask=%u;enabled=%d", this->revision_,
                this->state_machine_.get_alarm_hour(), this->state_machine_.get_alarm_minute(),
                this->state_machine_.get_days_mask(), this->state_machine_.get_alarm_enabled() ? 1 : 0);
  return buffer;
}

void VoiceAlarmClock::dump_config() {
  ESP_LOGCONFIG(TAG, "Voice Alarm Clock (scheduler scaffold)");
  ESP_LOGCONFIG(TAG, "  Request/ack schema version: %u", static_cast<unsigned>(kRequestSchemaVersion));
  ESP_LOGCONFIG(TAG, "  Time source: %s", clock_ == nullptr ? "injected by caller" : "configured");
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace voice_alarm
}  // namespace esphome
