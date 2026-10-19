#include "voice_alarm.h"

#include <cstdio>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace voice_alarm {

static const char *const TAG = "voice_alarm";

namespace {
constexpr uint32_t REVISION_PREF_MAGIC = 0x76617270;  // "varp", reserved for the clock revision.
constexpr uint32_t SCHEDULE_PREF_MAGIC = 0x76617363;  // "vasc", the versioned schedule blob.
}  // namespace

void VoiceAlarmClock::setup() {
  state_machine_.init();
  this->revision_preference_ = global_preferences->make_preference<uint32_t>(REVISION_PREF_MAGIC);
  this->revision_preference_.load(&this->revision_);
  this->schedule_preference_ = global_preferences->make_preference(kScheduleMaxSize, SCHEDULE_PREF_MAGIC);
  uint8_t buffer[kScheduleMaxSize];
  if (this->schedule_preference_.load(buffer, sizeof(buffer))) {
    if (!this->state_machine_.load_from(buffer, sizeof(buffer))) {
      // Preserve the stored bytes (no overwrite) and surface the error.
      this->state_machine_.report_storage_error("alarm schedule storage is corrupt or incompatible");
      ESP_LOGE(TAG, "Failed to load alarm schedule; preserving stored data");
    }
  }
}

bool VoiceAlarmClock::save_schedule() {
  uint8_t buffer[kScheduleMaxSize];
  if (!this->state_machine_.serialize_to(buffer, sizeof(buffer)))
    return false;
  return this->schedule_preference_.save(buffer, sizeof(buffer)) && global_preferences->sync();
}

void VoiceAlarmClock::update() {
  if (this->clock_ == nullptr)
    return;
  const auto now = this->clock_->now();
  if (!now.is_valid())
    return;
  this->state_machine_.tick(now.timestamp, millis());
  if (this->state_machine_.transition_pending()) {
    this->state_machine_.clear_transition_pending();
    if (!this->save_schedule())
      ESP_LOGE(TAG, "Failed to persist alarm occurrence transition");
  }
}

RequestResult VoiceAlarmClock::process_set_alarm_request(uint32_t request_id, uint32_t expected_revision,
                                                          uint8_t hour, uint8_t minute, uint8_t days_mask) {
  const SetAlarmRequest request{request_id, expected_revision, hour, minute, days_mask};
  RequestResult result = esphome::voice_alarm::process_set_alarm_request(request, this->revision_);
  if (result.status == RequestStatus::OK) {
    const std::time_t now =
        (this->clock_ != nullptr && this->clock_->now().is_valid()) ? this->clock_->now().timestamp : 0;
    const MutateResult mutate = this->state_machine_.upsert_repeating(hour, minute, days_mask, now);
    if (mutate.status != MutateStatus::OK) {
      result.status = this->map_mutate_error(mutate.status);
      result.revision = this->revision_;
    } else {
      // Snapshot the current schedule so a failed durable save can roll back.
      uint8_t snapshot[kScheduleMaxSize];
      this->state_machine_.serialize_to(snapshot, sizeof(snapshot));
      const uint32_t old_revision = this->revision_;
      this->revision_ = result.revision;
      if (!this->save_schedule() || !this->revision_preference_.save(&this->revision_) ||
          !global_preferences->sync()) {
        // An in-memory update is not an acknowledgment: roll back and report the error.
        this->state_machine_.load_from(snapshot, sizeof(snapshot));
        this->revision_ = old_revision;
        result.status = RequestStatus::STORAGE_ERROR;
        result.revision = old_revision;
        ESP_LOGE(TAG, "Failed to persist alarm schedule; change rolled back");
      }
    }
  }
  this->last_result_ = result;
  return result;
}

RequestStatus VoiceAlarmClock::map_mutate_error(MutateStatus status) const {
  switch (status) {
    case MutateStatus::INVALID_HOUR:
      return RequestStatus::INVALID_HOUR;
    case MutateStatus::INVALID_MINUTE:
      return RequestStatus::INVALID_MINUTE;
    case MutateStatus::INVALID_DAYS_MASK:
      return RequestStatus::INVALID_DAYS_MASK;
    case MutateStatus::CAPACITY:
    case MutateStatus::UNKNOWN_ID:
    case MutateStatus::INVALID_DATE:
    case MutateStatus::PAST_ONE_SHOT:
    case MutateStatus::NONEXISTENT_TIME:
      return RequestStatus::STORAGE_ERROR;
  }
  return RequestStatus::STORAGE_ERROR;
}

void VoiceAlarmClock::tick_and_persist(std::time_t now, uint32_t mono_ms) {
  this->state_machine_.tick(now, mono_ms);
  if (this->state_machine_.transition_pending()) {
    this->state_machine_.clear_transition_pending();
    if (!this->save_schedule())
      ESP_LOGE(TAG, "Failed to persist alarm occurrence transition");
  }
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
      return "days_mask must be 1..127 for a repeating alarm";
    case RequestStatus::STORAGE_ERROR:
      return "the alarm schedule could not be applied";
  }
  return "invalid request";
}

std::string VoiceAlarmClock::snapshot() const {
  char buffer[96];
  const AlarmEntry *entry = this->state_machine_.first_repeating();
  std::snprintf(buffer, sizeof(buffer), "revision=%u;hour=%u;minute=%u;days_mask=%u;enabled=%d", this->revision_,
                entry != nullptr ? entry->hour : 0, entry != nullptr ? entry->minute : 0,
                entry != nullptr ? entry->days_mask : 0, (entry != nullptr && entry->enabled) ? 1 : 0);
  return buffer;
}

void VoiceAlarmClock::dump_config() {
  ESP_LOGCONFIG(TAG, "Voice Alarm Clock (scheduler)");
  ESP_LOGCONFIG(TAG, "  Request/ack schema version: %u", static_cast<unsigned>(kRequestSchemaVersion));
  ESP_LOGCONFIG(TAG, "  Time source: %s", this->clock_ == nullptr ? "injected by caller" : "configured");
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace voice_alarm
}  // namespace esphome
