#include "voice_alarm.h"
#include "esphome/core/log.h"

namespace esphome {
namespace voice_alarm {

static const char *const TAG = "voice_alarm";

void VoiceAlarmClock::setup() { state_machine_.init(); }

void VoiceAlarmClock::update() {
  if (clock_ == nullptr)
    return;
  const auto now = clock_->now();
  if (now.is_valid())
    state_machine_.tick(now.timestamp);
}

void VoiceAlarmClock::dump_config() {
  ESP_LOGCONFIG(TAG, "Voice Alarm Clock (scheduler scaffold)");
  ESP_LOGCONFIG(TAG, "  Time source: %s", clock_ == nullptr ? "injected by caller" : "configured");
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace voice_alarm
}  // namespace esphome
