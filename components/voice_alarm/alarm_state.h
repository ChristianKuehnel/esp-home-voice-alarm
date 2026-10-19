#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace esphome {
namespace voice_alarm {

constexpr uint8_t kDefaultAlarmCapacity = 8;
constexpr int32_t kCatchUpSeconds = 300;
constexpr uint32_t kRingTimeoutMs = 15u * 60u * 1000u;
constexpr size_t kFireLogCapacity = 16;

// Serialization (M2.4): an explicit, versioned, little-endian byte layout. No
// raw, compiler-dependent structs are persisted.
constexpr uint32_t kScheduleMagic = 0x76617363;  // "vasc"
constexpr uint16_t kScheduleVersion = 1;
constexpr size_t kScheduleHeaderSize = 12;
constexpr size_t kScheduleEntrySize = 19;
constexpr size_t kScheduleMaxSize = kScheduleHeaderSize + kDefaultAlarmCapacity * kScheduleEntrySize;

// Monday-first weekday bits: bit 0 = Monday ... bit 6 = Sunday.
// Weekdays = 31, daily = 127. A one-shot schedule uses mask 0.
constexpr uint8_t kWeekdayMask = 31;
constexpr uint8_t kDailyMask = 127;

enum class AlarmState {
  WAITING_FOR_TIME = 0,
  IDLE = 1,
  ARMED = 2,
  RINGING = 3,
  ERROR = 4,
};

enum class AlarmKind {
  REPEATING = 0,
  ONE_SHOT = 1,
};

// Calendar identity of an occurrence: the local (year, month, day, hour). A
// repeated autumn hour shares one key, and a backward clock correction that
// re-encounters the same wall hour must not refire it.
struct OccurrenceKey {
  uint16_t year{0};
  uint8_t month{0};
  uint8_t day{0};
  uint8_t hour{0};

  bool operator==(const OccurrenceKey &other) const {
    return year == other.year && month == other.month && day == other.day && hour == other.hour;
  }
};

struct AlarmEntry {
  bool used{false};
  uint32_t id{0};
  AlarmKind kind{AlarmKind::REPEATING};
  bool enabled{true};
  uint8_t hour{0};
  uint8_t minute{0};
  uint8_t days_mask{0};  // Repeating only.
  uint16_t year{0};      // One-shot only.
  uint8_t month{0};
  uint8_t day{0};
  // Derived at run time; never persisted.
  bool has_next_due{false};
  std::time_t next_due{0};
  // The last occurrence this entry processed (fired or skipped as stale).
  bool has_handled_key{false};
  OccurrenceKey handled_key{};
};

// A live firing event. Emitted once per occurrence; missed events are not
// replayed. New entries evict the oldest.
struct FiredOccurrence {
  uint32_t entry_id{0};
  std::time_t due{0};
  std::time_t observed{0};
  uint32_t session_id{0};
};

enum class MutateStatus {
  OK = 0,
  CAPACITY = 1,
  UNKNOWN_ID = 2,
  INVALID_HOUR = 3,
  INVALID_MINUTE = 4,
  INVALID_DAYS_MASK = 5,
  INVALID_DATE = 6,
  PAST_ONE_SHOT = 7,
  NONEXISTENT_TIME = 8,
};

struct MutateResult {
  MutateStatus status;
  uint32_t id;
};

// Pure scheduler: no audio, network, or real clock. The caller injects wall
// time and monotonic elapsed time so the same core runs on desktop, host, and
// Xtensa. Schedules are separate from the ringing session; both are evaluated
// on every tick.
class AlarmStateMachine {
 public:
  AlarmStateMachine() = default;

  void init();
  void init(uint8_t capacity);

  // Mutations (M2.1). `now` is the injected wall epoch; pass 0 to defer
  // strictly-future resolution to the first tick (the boot-synchronization case).
  MutateResult add_repeating(uint8_t hour, uint8_t minute, uint8_t days_mask, std::time_t now);
  MutateResult add_one_shot(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                            std::time_t now);
  // Targets the first repeating entry, updating it in place, or creates one.
  MutateResult upsert_repeating(uint8_t hour, uint8_t minute, uint8_t days_mask, std::time_t now);
  MutateResult remove_entry(uint32_t id);
  MutateResult remove_all();
  MutateResult set_enabled(uint32_t id, bool enabled);

  // Advances the scheduler (M2.2/M2.3). `now` is wall time, `mono_ms` is
  // monotonic elapsed milliseconds used for the ringing timeout.
  void tick(std::time_t now, uint32_t mono_ms);

  // Dismisses the active ringing session at the last observed wall time.
  // Consumes every occurrence already due and preserves future schedules.
  // A no-op when no session is active.
  void stop();

  AlarmState get_state() const;
  const std::string &error_reason() const { return error_reason_; }
  void report_storage_error(const std::string &reason);
  void clear_storage_error();

  const AlarmEntry *find_entry(uint32_t id) const;
  const AlarmEntry *first_repeating() const;
  size_t entry_count() const;
  const std::vector<AlarmEntry> &entries() const { return entries_; }
  uint8_t capacity() const { return capacity_; }

  bool session_active() const { return session_active_; }
  uint32_t session_id() const { return session_id_; }

  size_t fire_log_size() const { return fire_log_.size(); }
  const FiredOccurrence &fire_log_at(size_t index) const { return fire_log_.at(index); }

  // True when a tick consumed or released an occurrence, so the caller can
  // persist the occurrence bookkeeping now (never per tick otherwise).
  bool transition_pending() const { return transition_pending_; }
  void clear_transition_pending() { transition_pending_ = false; }

  // Serializes the schedule (entries + ID allocator) into `out`, which must be
  // at least kScheduleMaxSize bytes. Returns false if the buffer is too small.
  bool serialize_to(uint8_t *out, size_t out_size) const;
  // Loads a schedule from `data`. Returns false, leaving the machine unchanged,
  // on corruption, an unknown version, or an incompatible entry count.
  bool load_from(const uint8_t *data, size_t size);

 protected:
  bool compute_next_due(AlarmEntry &entry, std::time_t ref);
  void mark_handled_through(AlarmEntry &entry, std::time_t now);
  void consume_due(AlarmEntry &entry, std::time_t now);
  void end_session();
  void push_fire_log(const FiredOccurrence &fire);

  uint8_t capacity_{kDefaultAlarmCapacity};
  std::vector<AlarmEntry> entries_;
  uint32_t next_id_{1};
  bool session_active_{false};
  uint32_t session_id_{0};
  uint32_t session_start_mono_{0};
  bool never_ticked_{true};
  std::time_t last_now_{0};
  uint32_t last_mono_ms_{0};
  bool storage_error_{false};
  std::string error_reason_;
  bool transition_pending_{false};
  std::vector<FiredOccurrence> fire_log_;
};

}  // namespace voice_alarm
}  // namespace esphome
