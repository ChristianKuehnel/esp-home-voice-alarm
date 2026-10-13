#pragma once

#include "esphome/core/preferences.h"

// A preferences probe for testing each platform's storage backend. It is NOT
// the future alarm storage format and must never be included in a device recipe.
namespace voice_alarm_test {
inline esphome::ESPPreferenceObject preference;
inline uint32_t probe = 0;
inline bool save_probe(uint32_t value) {
  if (!preference.save(&value) || !esphome::global_preferences->sync()) return false;
  probe = value;
  return true;
}
inline bool setup_preferences() {
  preference = esphome::global_preferences->make_preference<uint32_t>(0x76617431);
  preference.load(&probe);
  return save_probe(probe + 1);
}
}  // namespace voice_alarm_test
