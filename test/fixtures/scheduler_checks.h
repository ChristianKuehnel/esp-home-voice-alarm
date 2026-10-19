#pragma once

#include <algorithm>

#include "esphome/components/voice_alarm/alarm_state.h"

// The same assertions run on the native, ESPHome host, and Xtensa builds.
// Callers set TZ=UTC0, so every epoch here is a UTC wall time. DST-specific
// cases live in dst_checks.h and run natively only, under a real DST zone.
inline const char *scheduler_checks() {
  using esphome::voice_alarm::AlarmEntry;
  using esphome::voice_alarm::AlarmKind;
  using esphome::voice_alarm::AlarmState;
  using esphome::voice_alarm::AlarmStateMachine;
  using esphome::voice_alarm::MutateStatus;
  using esphome::voice_alarm::kScheduleMaxSize;
  constexpr std::time_t monday = 1791788400;  // 2026-10-12 07:00:00 UTC.

  // --- Time-validity states -------------------------------------------------
  {
    AlarmStateMachine m;
    m.init();
    if (m.get_state() != AlarmState::WAITING_FOR_TIME) return "initial state not waiting_for_time";
    m.tick(monday, 1000);
    if (m.get_state() != AlarmState::IDLE) return "empty scheduler not idle after first tick";
    m.add_repeating(7, 0, 127, monday);
    if (m.get_state() != AlarmState::ARMED) return "scheduler not armed after add";
  }

  // --- M2.1: capacity and stable, non-renumbering IDs -----------------------
  {
    AlarmStateMachine m;
    m.init();
    for (int i = 0; i < 8; ++i) {
      if (m.add_repeating(7, 0, 127, monday).status != MutateStatus::OK) return "capacity create rejected";
    }
    if (m.entry_count() != 8) return "capacity count wrong";
    if (m.add_repeating(8, 0, 127, monday).status != MutateStatus::CAPACITY) return "ninth create accepted";
    if (m.entry_count() != 8) return "overflow changed entry count";
    if (m.find_entry(1) == nullptr || m.find_entry(8) == nullptr) return "overflow clobbered entries";
  }
  {
    AlarmStateMachine m;
    m.init();
    const auto a = m.add_repeating(7, 0, 1, monday);
    const auto b = m.add_repeating(8, 0, 2, monday);
    const auto c = m.add_repeating(9, 0, 4, monday);
    if (m.remove_entry(a.id).status != MutateStatus::OK) return "remove failed";
    if (m.find_entry(a.id) != nullptr) return "removed entry still present";
    if (m.find_entry(b.id) == nullptr || m.find_entry(c.id) == nullptr) return "remove renumbered ids";
    if (m.remove_entry(9999).status != MutateStatus::UNKNOWN_ID) return "unknown id remove accepted";
  }

  // --- M2.1: field validation ------------------------------------------------
  {
    AlarmStateMachine m;
    m.init();
    if (m.add_repeating(24, 0, 1, monday).status != MutateStatus::INVALID_HOUR) return "hour 24 accepted";
    if (m.add_repeating(0, 60, 1, monday).status != MutateStatus::INVALID_MINUTE) return "minute 60 accepted";
    if (m.add_repeating(0, 0, 0, monday).status != MutateStatus::INVALID_DAYS_MASK) return "mask 0 repeating accepted";
    if (m.add_repeating(0, 0, 128, monday).status != MutateStatus::INVALID_DAYS_MASK) return "mask 128 accepted";
    if (m.entry_count() != 0) return "invalid creates left entries";
  }

  // --- M2.1: one-shot validation and release ---------------------------------
  {
    AlarmStateMachine m;
    m.init();
    if (m.add_one_shot(2026, 10, 12, 7, 0, monday).status != MutateStatus::PAST_ONE_SHOT)
      return "one-shot at now accepted";
    if (m.add_one_shot(2026, 10, 12, 6, 0, monday).status != MutateStatus::PAST_ONE_SHOT)
      return "past one-shot accepted";
    if (m.add_one_shot(2026, 2, 30, 7, 0, monday).status != MutateStatus::INVALID_DATE) return "Feb 30 accepted";
    if (m.add_one_shot(2026, 10, 12, 24, 0, monday).status != MutateStatus::INVALID_HOUR)
      return "one-shot hour 24 accepted";
    if (m.add_one_shot(2026, 10, 13, 7, 0, monday).status != MutateStatus::OK) return "future one-shot rejected";
  }
  {
    AlarmStateMachine m;
    m.init();
    const auto r = m.add_one_shot(2026, 10, 12, 8, 0, monday - 3600);  // due 08:00, now 06:00.
    if (r.status != MutateStatus::OK) return "one-shot create failed";
    m.tick(monday + 3600, 1000);  // 08:00:00 == due.
    if (m.get_state() != AlarmState::RINGING) return "one-shot did not fire";
    if (m.find_entry(r.id) != nullptr) return "one-shot entry not released";
    m.stop();
    if (m.get_state() != AlarmState::IDLE) return "one-shot stop left state";
  }

  // --- M2.2: a bare time means the next strictly future occurrence -----------
  {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 127, monday - 3600);  // now 06:00.
    m.tick(monday - 3600, 1000);
    if (m.get_state() != AlarmState::ARMED) return "bare time not armed";
    m.tick(monday, 2000);  // today 07:00.
    if (m.get_state() != AlarmState::RINGING) return "bare time did not fire today";
    m.stop();
  }
  {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 127, monday + 3600);  // now 08:00.
    m.tick(monday + 3600, 1000);
    if (m.get_state() == AlarmState::RINGING) return "bare time fired for a past hour";
    m.tick(monday + 86400, 2000);  // tomorrow 07:00.
    if (m.get_state() != AlarmState::RINGING) return "bare time did not fire tomorrow";
    m.stop();
  }

  // --- M2.2: inclusive 300 s catch-up at boot synchronization ----------------
  for (int offset = 299; offset <= 301; ++offset) {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 127, 0);  // now=0 defers; the crossing is catch-up-eligible.
    m.tick(monday + offset, 1000);  // first tick at due+offset.
    const bool should_ring = offset <= 300;
    if (should_ring && m.get_state() != AlarmState::RINGING) return "catch-up did not ring in window";
    if (!should_ring && m.get_state() == AlarmState::RINGING) return "catch-up rang outside window";
    m.stop();
    m.tick(monday + 86400, 2000);  // the next repeating occurrence must remain.
    if (m.get_state() != AlarmState::RINGING) return "future repeating occurrence lost";
    m.stop();
  }

  // --- M2.2: a deadline crossed between ticks still fires --------------------
  {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 127, monday - 7200);  // now 05:00.
    m.tick(monday - 61, 1000);  // 06:59:59, just before.
    if (m.get_state() == AlarmState::RINGING) return "fired before the deadline";
    m.tick(monday + 30, 2000);  // 07:00:30, crossed without observing 07:00:00.
    if (m.get_state() != AlarmState::RINGING) return "delayed tick did not fire";
    m.stop();
  }

  // --- M2.2: clock corrections -----------------------------------------------
  {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 127, monday - 7200);
    m.tick(monday - 3600, 1000);  // 06:00, no fire.
    m.tick(monday + 299, 2000);   // clock jumps forward to 07:04:59.
    if (m.get_state() != AlarmState::RINGING) return "forward correction did not catch up";
    m.stop();
  }
  {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 127, monday - 7200);
    m.tick(monday, 1000);  // 07:00:00, fire.
    if (m.get_state() != AlarmState::RINGING) return "did not fire at the deadline";
    m.stop();
    m.tick(monday - 60, 2000);  // clock rolled back to 06:59:00.
    if (m.get_state() == AlarmState::RINGING) return "backward correction refired a handled occurrence";
  }

  // --- M2.3: overlapping alarms share one session ----------------------------
  {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 127, monday - 7200);   // A: daily 07:00.
    m.add_repeating(7, 0, 1, monday - 7200);     // B: Monday 07:00.
    m.add_repeating(7, 30, 127, monday - 7200);  // C: daily 07:30.
    m.tick(monday, 1000);  // 07:00: A and B fire, one session.
    if (m.get_state() != AlarmState::RINGING) return "overlap did not ring";
    if (m.session_id() != 1) return "overlap session id wrong";
    if (m.fire_log_size() != 2) return "overlap fire log wrong";
    m.tick(monday + 1800, 2000);  // 07:30: C joins the same session.
    if (m.fire_log_size() != 3) return "late arrival not logged";
    if (m.session_id() != 1) return "late arrival started a new session";
    m.stop();  // one dismissal consumes every occurrence already due.
    if (m.session_active()) return "dismissal did not end the session";
    m.tick(monday + 86400, 3000);  // future occurrences remain.
    if (m.get_state() != AlarmState::RINGING) return "future occurrence lost after dismissal";
    m.stop();
  }

  // --- M2.3: 15-minute monotonic timeout -------------------------------------
  {
    AlarmStateMachine m;
    m.init();
    m.add_repeating(7, 0, 1, monday - 7200);  // Monday 07:00 only.
    m.tick(monday, 0);  // fire; the session starts at mono 0.
    if (m.get_state() != AlarmState::RINGING) return "timeout: did not ring";
    m.tick(monday + 86400, 14 * 60 * 1000);  // wall jumps a day; mono is 14 min in.
    if (!m.session_active()) return "timeout: wall jump ended the session";
    m.add_repeating(8, 0, 127, monday + 86400);  // a later arrival.
    m.tick(monday + 86400 + 3600, 14 * 60 * 1000 + 1000);
    if (!m.session_active()) return "timeout: arrival ended the session early";
    m.tick(monday + 86400 + 3600, 15 * 60 * 1000 - 1);
    if (!m.session_active()) return "timeout: ended before 15 minutes";
    m.tick(monday + 86400 + 3600, 15 * 60 * 1000);
    if (m.session_active()) return "timeout: not ended at 15 minutes";
  }

  // --- M2.3: stop semantics --------------------------------------------------
  {
    AlarmStateMachine m;
    m.init();
    m.stop();  // idle stop is a no-op.
    if (m.get_state() != AlarmState::WAITING_FOR_TIME) return "idle stop changed state";
    m.add_repeating(7, 0, 127, monday - 7200);
    m.tick(monday, 1000);  // fire.
    m.stop();
    if (m.session_active()) return "stop did not end the session";
    if (m.entry_count() != 1) return "stop deleted the schedule";
    if (m.get_state() != AlarmState::ARMED) return "stop lost the armed schedule";
  }

  // --- Error state ------------------------------------------------------------
  {
    AlarmStateMachine m;
    m.init();
    m.report_storage_error("corrupt");
    if (m.get_state() != AlarmState::ERROR) return "storage error not surfaced";
    if (m.error_reason() != "corrupt") return "storage error reason lost";
    m.clear_storage_error();
    m.tick(monday, 1000);
    if (m.get_state() != AlarmState::IDLE) return "storage error not cleared";
  }

  // --- M2.4: serialization round trip and corruption --------------------------
  {
    AlarmStateMachine m;
    m.init();
    const auto a = m.add_repeating(7, 30, 31, monday);
    const auto b = m.add_one_shot(2026, 10, 13, 9, 15, monday);
    uint8_t buffer[kScheduleMaxSize];
    if (!m.serialize_to(buffer, sizeof(buffer))) return "serialize failed";
    AlarmStateMachine m2;
    m2.init();
    if (!m2.load_from(buffer, sizeof(buffer))) return "load_from failed on a valid blob";
    if (m2.entry_count() != 2) return "round-trip count wrong";
    const AlarmEntry *ea = m2.find_entry(a.id);
    const AlarmEntry *eb = m2.find_entry(b.id);
    if (ea == nullptr || eb == nullptr) return "round-trip lost ids";
    if (ea->hour != 7 || ea->minute != 30 || ea->days_mask != 31) return "round-trip repeating fields wrong";
    if (eb->kind != AlarmKind::ONE_SHOT || eb->year != 2026 || eb->month != 10 || eb->day != 13)
      return "round-trip one-shot fields wrong";
    // A corrupted magic is rejected and leaves the machine unchanged.
    uint8_t corrupt[kScheduleMaxSize];
    std::copy(buffer, buffer + sizeof(buffer), corrupt);
    corrupt[0] ^= 0xff;
    if (m2.load_from(corrupt, sizeof(corrupt))) return "corrupt magic accepted";
    if (m2.entry_count() != 2) return "corrupt load modified the machine";
    // An unknown version is rejected.
    uint8_t badver[kScheduleMaxSize];
    std::copy(buffer, buffer + sizeof(buffer), badver);
    badver[4] = 99;
    if (m2.load_from(badver, sizeof(badver))) return "unknown version accepted";
    // An entry count above capacity is rejected.
    uint8_t big[kScheduleMaxSize];
    std::copy(buffer, buffer + sizeof(buffer), big);
    big[6] = 9;
    if (m2.load_from(big, sizeof(big))) return "incompatible capacity accepted";
  }
  return nullptr;
}
