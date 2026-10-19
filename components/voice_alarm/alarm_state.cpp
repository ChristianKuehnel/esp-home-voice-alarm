#include "alarm_state.h"

#include <algorithm>

namespace esphome {
namespace voice_alarm {

namespace {

bool is_leap(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(uint16_t year, uint8_t month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12)
    return 0;
  if (month == 2 && is_leap(year))
    return 29;
  return kDays[month - 1];
}

bool valid_calendar_date(uint16_t year, uint8_t month, uint8_t day) {
  if (year < 1971 || year > 2100)
    return false;
  return day >= 1 && day <= static_cast<uint8_t>(days_in_month(year, month));
}

std::time_t mk_local(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, int isdst) {
  std::tm t{};
  t.tm_year = static_cast<int>(year) - 1900;
  t.tm_mon = static_cast<int>(month) - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = 0;
  t.tm_isdst = isdst;
  return mktime(&t);
}

bool round_trips(std::time_t epoch, uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  const std::tm *t = std::localtime(&epoch);
  if (t == nullptr)
    return false;
  return static_cast<uint16_t>(1900 + t->tm_year) == year && static_cast<uint8_t>(t->tm_mon + 1) == month &&
         static_cast<uint8_t>(t->tm_mday) == day && static_cast<uint8_t>(t->tm_hour) == hour &&
         static_cast<uint8_t>(t->tm_min) == minute;
}

// The earliest valid epoch for a local date/time, or -1 when the time does not
// exist (a spring DST gap). An ambiguous autumn time yields two candidates and
// the earlier one is returned, per the ADR-009 first-occurrence rule.
std::time_t first_occurrence(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  if (!valid_calendar_date(year, month, day))
    return -1;
  std::time_t best = -1;
  for (int isdst = 0; isdst <= 1; ++isdst) {
    const std::time_t cand = mk_local(year, month, day, hour, minute, isdst);
    if (cand == -1)
      continue;
    if (!round_trips(cand, year, month, day, hour, minute))
      continue;
    if (best == -1 || cand < best)
      best = cand;
  }
  return best;
}

OccurrenceKey key_of(std::time_t epoch) {
  const std::tm *t = std::localtime(&epoch);
  if (t == nullptr)
    return OccurrenceKey{};
  return OccurrenceKey{static_cast<uint16_t>(1900 + t->tm_year), static_cast<uint8_t>(t->tm_mon + 1),
                       static_cast<uint8_t>(t->tm_mday), static_cast<uint8_t>(t->tm_hour)};
}

bool key_before(const OccurrenceKey &a, const OccurrenceKey &b) {
  if (a.year != b.year)
    return a.year < b.year;
  if (a.month != b.month)
    return a.month < b.month;
  if (a.day != b.day)
    return a.day < b.day;
  return a.hour < b.hour;
}

// The noon anchor of the local date containing `epoch`; noon exists in every
// practical zone, even where midnight does not.
std::time_t noon_anchor(std::time_t epoch) {
  const std::tm *t = std::localtime(&epoch);
  if (t == nullptr)
    return -1;
  std::tm anchor = *t;
  anchor.tm_hour = 12;
  anchor.tm_min = 0;
  anchor.tm_sec = 0;
  return mktime(&anchor);
}

}  // namespace

void AlarmStateMachine::init() { init(kDefaultAlarmCapacity); }

void AlarmStateMachine::init(uint8_t capacity) {
  capacity_ = capacity;
  entries_.assign(capacity, AlarmEntry{});
  next_id_ = 1;
  session_active_ = false;
  session_id_ = 0;
  session_start_mono_ = 0;
  never_ticked_ = true;
  last_now_ = 0;
  last_mono_ms_ = 0;
  storage_error_ = false;
  error_reason_.clear();
  transition_pending_ = false;
  fire_log_.clear();
}

MutateResult AlarmStateMachine::add_repeating(uint8_t hour, uint8_t minute, uint8_t days_mask, std::time_t now) {
  MutateResult result{MutateStatus::OK, 0};
  if (hour > 23)
    return {MutateStatus::INVALID_HOUR, 0};
  if (minute > 59)
    return {MutateStatus::INVALID_MINUTE, 0};
  if (days_mask == 0 || days_mask > 127)
    return {MutateStatus::INVALID_DAYS_MASK, 0};
  int free_slot = -1;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (!entries_[i].used) {
      free_slot = static_cast<int>(i);
      break;
    }
  }
  if (free_slot < 0)
    return {MutateStatus::CAPACITY, 0};
  AlarmEntry &e = entries_[free_slot];
  e.used = true;
  e.id = next_id_++;
  e.kind = AlarmKind::REPEATING;
  e.enabled = true;
  e.hour = hour;
  e.minute = minute;
  e.days_mask = days_mask;
  e.year = 0;
  e.month = 0;
  e.day = 0;
  e.has_next_due = false;
  e.next_due = 0;
  e.has_handled_key = false;
  e.handled_key = OccurrenceKey{};
  if (now > 0)
    mark_handled_through(e, now);
  result.id = e.id;
  transition_pending_ = true;
  return result;
}

MutateResult AlarmStateMachine::add_one_shot(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                                             uint8_t minute, std::time_t now) {
  MutateResult result{MutateStatus::OK, 0};
  if (hour > 23)
    return {MutateStatus::INVALID_HOUR, 0};
  if (minute > 59)
    return {MutateStatus::INVALID_MINUTE, 0};
  if (!valid_calendar_date(year, month, day))
    return {MutateStatus::INVALID_DATE, 0};
  const std::time_t due = first_occurrence(year, month, day, hour, minute);
  if (due == -1)
    return {MutateStatus::NONEXISTENT_TIME, 0};
  if (now > 0 && due <= now)
    return {MutateStatus::PAST_ONE_SHOT, 0};
  int free_slot = -1;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (!entries_[i].used) {
      free_slot = static_cast<int>(i);
      break;
    }
  }
  if (free_slot < 0)
    return {MutateStatus::CAPACITY, 0};
  AlarmEntry &e = entries_[free_slot];
  e.used = true;
  e.id = next_id_++;
  e.kind = AlarmKind::ONE_SHOT;
  e.enabled = true;
  e.hour = hour;
  e.minute = minute;
  e.days_mask = 0;
  e.year = year;
  e.month = month;
  e.day = day;
  e.has_next_due = false;
  e.next_due = 0;
  e.has_handled_key = false;
  e.handled_key = OccurrenceKey{};
  result.id = e.id;
  transition_pending_ = true;
  return result;
}

MutateResult AlarmStateMachine::upsert_repeating(uint8_t hour, uint8_t minute, uint8_t days_mask, std::time_t now) {
  if (hour > 23)
    return {MutateStatus::INVALID_HOUR, 0};
  if (minute > 59)
    return {MutateStatus::INVALID_MINUTE, 0};
  if (days_mask == 0 || days_mask > 127)
    return {MutateStatus::INVALID_DAYS_MASK, 0};
  for (auto &e : entries_) {
    if (e.used && e.kind == AlarmKind::REPEATING) {
      e.hour = hour;
      e.minute = minute;
      e.days_mask = days_mask;
      e.enabled = true;
      e.has_next_due = false;
      if (now > 0)
        mark_handled_through(e, now);
      transition_pending_ = true;
      return {MutateStatus::OK, e.id};
    }
  }
  return add_repeating(hour, minute, days_mask, now);
}

MutateResult AlarmStateMachine::remove_entry(uint32_t id) {
  for (auto &e : entries_) {
    if (e.used && e.id == id) {
      e.used = false;
      e.has_next_due = false;
      transition_pending_ = true;
      return {MutateStatus::OK, id};
    }
  }
  return {MutateStatus::UNKNOWN_ID, 0};
}

MutateResult AlarmStateMachine::remove_all() {
  bool any = false;
  for (auto &e : entries_) {
    if (e.used) {
      e.used = false;
      e.has_next_due = false;
      any = true;
    }
  }
  if (any)
    transition_pending_ = true;
  return {MutateStatus::OK, 0};
}

MutateResult AlarmStateMachine::set_enabled(uint32_t id, bool enabled) {
  for (auto &e : entries_) {
    if (e.used && e.id == id) {
      e.enabled = enabled;
      if (!enabled)
        e.has_next_due = false;
      transition_pending_ = true;
      return {MutateStatus::OK, id};
    }
  }
  return {MutateStatus::UNKNOWN_ID, 0};
}

void AlarmStateMachine::tick(std::time_t now, uint32_t mono_ms) {
  never_ticked_ = false;
  last_now_ = now;
  last_mono_ms_ = mono_ms;

  for (auto &entry : entries_) {
    if (!entry.used || !entry.enabled)
      continue;
    for (int guard = 0; guard < 64; ++guard) {
      if (!entry.has_next_due) {
        if (!compute_next_due(entry, now)) {
          entry.used = false;
          entry.has_next_due = false;
          transition_pending_ = true;
          break;
        }
      }
      if (now < entry.next_due)
        break;
      const int32_t age = static_cast<int32_t>(now - entry.next_due);
      if (age <= kCatchUpSeconds) {
        consume_due(entry, now);
        break;
      }
      entry.handled_key = key_of(entry.next_due);
      entry.has_handled_key = true;
      entry.has_next_due = false;
      transition_pending_ = true;
    }
  }

  if (session_active_ && (mono_ms - session_start_mono_) >= kRingTimeoutMs)
    end_session();
}

void AlarmStateMachine::stop() { end_session(); }

void AlarmStateMachine::end_session() {
  if (!session_active_)
    return;
  for (auto &entry : entries_) {
    if (!entry.used || !entry.enabled)
      continue;
    if (!entry.has_next_due) {
      if (entry.kind == AlarmKind::ONE_SHOT)
        continue;
      if (!compute_next_due(entry, last_now_))
        continue;
    }
    if (last_now_ >= entry.next_due)
      consume_due(entry, last_now_);
  }
  session_active_ = false;
}

AlarmState AlarmStateMachine::get_state() const {
  if (storage_error_)
    return AlarmState::ERROR;
  if (never_ticked_)
    return AlarmState::WAITING_FOR_TIME;
  if (session_active_)
    return AlarmState::RINGING;
  for (const auto &e : entries_) {
    if (e.used && e.enabled)
      return AlarmState::ARMED;
  }
  return AlarmState::IDLE;
}

void AlarmStateMachine::report_storage_error(const std::string &reason) {
  storage_error_ = true;
  error_reason_ = reason;
}

void AlarmStateMachine::clear_storage_error() {
  storage_error_ = false;
  error_reason_.clear();
}

const AlarmEntry *AlarmStateMachine::find_entry(uint32_t id) const {
  for (const auto &e : entries_) {
    if (e.used && e.id == id)
      return &e;
  }
  return nullptr;
}

const AlarmEntry *AlarmStateMachine::first_repeating() const {
  for (const auto &e : entries_) {
    if (e.used && e.kind == AlarmKind::REPEATING)
      return &e;
  }
  return nullptr;
}

size_t AlarmStateMachine::entry_count() const {
  size_t count = 0;
  for (const auto &e : entries_) {
    if (e.used)
      ++count;
  }
  return count;
}

bool AlarmStateMachine::compute_next_due(AlarmEntry &entry, std::time_t ref) {
  if (entry.kind == AlarmKind::ONE_SHOT) {
    const std::time_t due = first_occurrence(entry.year, entry.month, entry.day, entry.hour, entry.minute);
    if (due == -1)
      return false;
    if (entry.has_handled_key && key_of(due) == entry.handled_key)
      return false;
    entry.next_due = due;
    entry.has_next_due = true;
    return true;
  }
  const std::time_t anchor = noon_anchor(ref);
  if (anchor == -1)
    return false;
  // Backward scan: the catch-up window is shorter than a day, so the two most
  // recent local dates cover every occurrence that could still be due.
  std::time_t best_past = -1;
  for (int back = 0; back <= 1; ++back) {
    const std::time_t probe = anchor - back * 86400;
    const std::tm *pt = std::localtime(&probe);
    if (pt == nullptr)
      continue;
    const uint8_t bit = 1u << ((pt->tm_wday + 6) % 7);
    if ((entry.days_mask & bit) == 0)
      continue;
    const std::time_t cand =
        first_occurrence(static_cast<uint16_t>(1900 + pt->tm_year), static_cast<uint8_t>(pt->tm_mon + 1),
                         static_cast<uint8_t>(pt->tm_mday), entry.hour, entry.minute);
    if (cand == -1 || cand > ref)
      continue;
    if (entry.has_handled_key && !key_before(entry.handled_key, key_of(cand)))
      continue;
    if (best_past == -1 || cand > best_past)
      best_past = cand;
  }
  if (best_past != -1) {
    entry.next_due = best_past;
    entry.has_next_due = true;
    return true;
  }
  // Forward scan: a single-weekday mask plus a skipped gap date needs at most
  // 14 days, so 16 local dates always suffice.
  for (int d = 0; d < 16; ++d) {
    const std::time_t probe = anchor + d * 86400;
    const std::tm *pt = std::localtime(&probe);
    if (pt == nullptr)
      continue;
    const uint8_t bit = 1u << ((pt->tm_wday + 6) % 7);
    if ((entry.days_mask & bit) == 0)
      continue;
    const std::time_t cand =
        first_occurrence(static_cast<uint16_t>(1900 + pt->tm_year), static_cast<uint8_t>(pt->tm_mon + 1),
                         static_cast<uint8_t>(pt->tm_mday), entry.hour, entry.minute);
    if (cand == -1 || cand <= ref)
      continue;
    if (entry.has_handled_key && !key_before(entry.handled_key, key_of(cand)))
      continue;
    entry.next_due = cand;
    entry.has_next_due = true;
    return true;
  }
  return false;
}

void AlarmStateMachine::mark_handled_through(AlarmEntry &entry, std::time_t now) {
  const std::time_t anchor = noon_anchor(now);
  if (anchor == -1)
    return;
  std::time_t best = -1;
  for (int back = 0; back <= 1; ++back) {
    const std::time_t probe = anchor - back * 86400;
    const std::tm *pt = std::localtime(&probe);
    if (pt == nullptr)
      continue;
    const uint8_t bit = 1u << ((pt->tm_wday + 6) % 7);
    if ((entry.days_mask & bit) == 0)
      continue;
    const std::time_t cand =
        first_occurrence(static_cast<uint16_t>(1900 + pt->tm_year), static_cast<uint8_t>(pt->tm_mon + 1),
                         static_cast<uint8_t>(pt->tm_mday), entry.hour, entry.minute);
    if (cand == -1 || cand > now)
      continue;
    if (best == -1 || cand > best)
      best = cand;
  }
  if (best != -1) {
    const OccurrenceKey k = key_of(best);
    if (!entry.has_handled_key || key_before(entry.handled_key, k)) {
      entry.handled_key = k;
      entry.has_handled_key = true;
    }
  }
}

void AlarmStateMachine::consume_due(AlarmEntry &entry, std::time_t now) {
  if (!session_active_) {
    session_active_ = true;
    session_start_mono_ = last_mono_ms_;
    ++session_id_;
  }
  const FiredOccurrence fire{entry.id, entry.next_due, now, session_id_};
  push_fire_log(fire);
  transition_pending_ = true;
  entry.handled_key = key_of(entry.next_due);
  entry.has_handled_key = true;
  entry.has_next_due = false;
  if (entry.kind == AlarmKind::ONE_SHOT)
    entry.used = false;
}

void AlarmStateMachine::push_fire_log(const FiredOccurrence &fire) {
  fire_log_.push_back(fire);
  if (fire_log_.size() > kFireLogCapacity)
    fire_log_.erase(fire_log_.begin());
}

bool AlarmStateMachine::serialize_to(uint8_t *out, size_t out_size) const {
  const size_t count = entry_count();
  if (out_size < kScheduleMaxSize)
    return false;
  std::fill(out, out + kScheduleMaxSize, 0);
  size_t o = 0;
  out[o++] = kScheduleMagic & 0xff;
  out[o++] = (kScheduleMagic >> 8) & 0xff;
  out[o++] = (kScheduleMagic >> 16) & 0xff;
  out[o++] = (kScheduleMagic >> 24) & 0xff;
  out[o++] = kScheduleVersion & 0xff;
  out[o++] = (kScheduleVersion >> 8) & 0xff;
  out[o++] = static_cast<uint8_t>(count);
  out[o++] = 0;  // reserved
  out[o++] = next_id_ & 0xff;
  out[o++] = (next_id_ >> 8) & 0xff;
  out[o++] = (next_id_ >> 16) & 0xff;
  out[o++] = (next_id_ >> 24) & 0xff;
  for (const auto &e : entries_) {
    if (!e.used)
      continue;
    out[o++] = e.id & 0xff;
    out[o++] = (e.id >> 8) & 0xff;
    out[o++] = (e.id >> 16) & 0xff;
    out[o++] = (e.id >> 24) & 0xff;
    out[o++] = static_cast<uint8_t>(e.kind);
    out[o++] = e.enabled ? 1 : 0;
    out[o++] = e.hour;
    out[o++] = e.minute;
    out[o++] = e.days_mask;
    out[o++] = e.year & 0xff;
    out[o++] = (e.year >> 8) & 0xff;
    out[o++] = e.month;
    out[o++] = e.day;
    out[o++] = e.has_handled_key ? 1 : 0;
    out[o++] = e.handled_key.year & 0xff;
    out[o++] = (e.handled_key.year >> 8) & 0xff;
    out[o++] = e.handled_key.month;
    out[o++] = e.handled_key.day;
    out[o++] = e.handled_key.hour;
  }
  return true;
}

bool AlarmStateMachine::load_from(const uint8_t *data, size_t size) {
  if (size < kScheduleMaxSize)
    return false;
  const uint32_t magic =
      data[0] | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) |
      (static_cast<uint32_t>(data[3]) << 24);
  if (magic != kScheduleMagic)
    return false;
  const uint16_t version = data[4] | (static_cast<uint16_t>(data[5]) << 8);
  if (version != kScheduleVersion)
    return false;
  const uint8_t count = data[6];
  if (data[7] != 0)
    return false;
  const uint32_t next_id = data[8] | (static_cast<uint32_t>(data[9]) << 8) | (static_cast<uint32_t>(data[10]) << 16) |
                           (static_cast<uint32_t>(data[11]) << 24);
  if (count > capacity_)
    return false;
  std::vector<AlarmEntry> parsed;
  parsed.reserve(count);
  size_t o = kScheduleHeaderSize;
  for (uint8_t i = 0; i < count; ++i) {
    if (o + kScheduleEntrySize > size)
      return false;
    AlarmEntry e;
    e.used = true;
    e.id = data[o] | (static_cast<uint32_t>(data[o + 1]) << 8) | (static_cast<uint32_t>(data[o + 2]) << 16) |
           (static_cast<uint32_t>(data[o + 3]) << 24);
    e.kind = (data[o + 4] == 1) ? AlarmKind::ONE_SHOT : AlarmKind::REPEATING;
    e.enabled = (data[o + 5] != 0);
    e.hour = data[o + 6];
    e.minute = data[o + 7];
    e.days_mask = data[o + 8];
    e.year = data[o + 9] | (static_cast<uint16_t>(data[o + 10]) << 8);
    e.month = data[o + 11];
    e.day = data[o + 12];
    e.has_handled_key = (data[o + 13] != 0);
    e.handled_key.year = data[o + 14] | (static_cast<uint16_t>(data[o + 15]) << 8);
    e.handled_key.month = data[o + 16];
    e.handled_key.day = data[o + 17];
    e.handled_key.hour = data[o + 18];
    e.has_next_due = false;  // Derived at run time; never persisted.
    e.next_due = 0;
    if (e.hour > 23 || e.minute > 59)
      return false;
    if (e.kind == AlarmKind::REPEATING) {
      if (e.days_mask == 0 || e.days_mask > 127)
        return false;
    } else if (!valid_calendar_date(e.year, e.month, e.day)) {
      return false;
    }
    parsed.push_back(e);
    o += kScheduleEntrySize;
  }
  // Every field validated: commit the parsed schedule.
  entries_.assign(capacity_, AlarmEntry{});
  for (size_t i = 0; i < parsed.size(); ++i)
    entries_[i] = parsed[i];
  next_id_ = next_id;
  return true;
}

}  // namespace voice_alarm
}  // namespace esphome
