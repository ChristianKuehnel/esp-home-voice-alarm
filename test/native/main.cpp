#include <cstdio>
#include <cstdlib>
#include "dst_checks.h"
#include "request_checks.h"
#include "scheduler_checks.h"

int main() {
  // scheduler_checks and request_checks are TZ-independent (the runner sets UTC0).
  if (const char *failure = scheduler_checks()) {
    std::fprintf(stderr, "FAIL: %s\n", failure);
    return 1;
  }
  if (const char *failure = request_checks()) {
    std::fprintf(stderr, "FAIL: %s\n", failure);
    return 1;
  }
  // dst_checks needs a real DST zone; the host and Xtensa targets do not
  // guarantee the zone data, so it runs only in the native layer.
  setenv("TZ", "America/New_York", 1);
  tzset();
  if (const char *failure = dst_checks()) {
    std::fprintf(stderr, "FAIL: %s\n", failure);
    return 1;
  }
  std::puts("VOICE_ALARM_NATIVE_PASS");
  return 0;
}
