#pragma once

#include <string>

#include "alarm_request.h"
#include "alarm_state.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace voice_alarm {

// Alarm volume contract (PRD): range 0.1..1.0, step 0.01, default 0.7. The
// step is enforced by the entity layer; the component clamps to the range.
constexpr float kAlarmVolumeDefault = 0.7f;
constexpr float kAlarmVolumeMin = 0.1f;
constexpr float kAlarmVolumeMax = 1.0f;

// Hardware callbacks live in the device recipe. Both targets share this core.
class VoiceAlarmClock : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  void set_time_source(time::RealTimeClock *clock) { clock_ = clock; }
  AlarmStateMachine &state_machine() { return state_machine_; }

  uint32_t current_revision() const { return revision_; }
  const RequestResult &last_result() const { return last_result_; }
  std::string last_error_message() const;

  // Runs the request through the pure processor, applies accepted requests to the
  // state machine, and commits the new revision to durable storage before
  // returning. days_mask 0 creates a one-shot alarm at year/month/day (the
  // date fields are ignored for repeating alarms). Rejected requests change
  // neither the schedule nor the revision.
  RequestResult process_set_alarm_request(uint32_t request_id, uint32_t expected_revision, uint8_t hour,
                                          uint8_t minute, uint8_t days_mask, uint16_t year, uint8_t month,
                                          uint8_t day);

  // Key=value snapshot for HA refresh, e.g.
  // "revision=1;hour=7;minute=0;days_mask=31;enabled=1;volume=0.70", plus a
  // ";one_shot=YYYY-MM-DD HH:MM" segment while a one-shot alarm is scheduled.
  std::string snapshot() const;

  // Test hook for the clock-less host fixture: advances the scheduler and
  // persists any occurrence transition, mirroring update().
  void tick_and_persist(std::time_t now, uint32_t mono_ms);

  // Persistent, independent alarm volume (M3.1). Persisted separately from the
  // schedule and from the media player volume. Out-of-range values are clamped
  // to [kAlarmVolumeMin, kAlarmVolumeMax] before storage.
  float alarm_volume() const { return alarm_volume_; }
  void set_alarm_volume(float volume);

  // Dismisses the active ringing session immediately (M3.2) and persists the
  // resulting occurrence bookkeeping. Returns true if a session was active.
  // A no-op when no session is active.
  bool dismiss_session();

  // Fires the registered automations on every scheduler state transition.
  template<typename F> void add_on_state_callback(F &&callback) {
    this->state_callback_.add(std::forward<F>(callback));
  }

 protected:
  RequestStatus map_mutate_error(MutateStatus status) const;
  bool save_schedule();
  void note_state_change();

  time::RealTimeClock *clock_{nullptr};
  AlarmStateMachine state_machine_;
  ESPPreferenceObject revision_preference_;
  ESPPreferenceObject schedule_preference_;
  ESPPreferenceObject volume_preference_;
  uint32_t revision_{0};
  RequestResult last_result_{};
  float alarm_volume_{kAlarmVolumeDefault};
  CallbackManager<void(AlarmState)> state_callback_;
  AlarmState last_state_{AlarmState::WAITING_FOR_TIME};
};

}  // namespace voice_alarm
}  // namespace esphome
