#include <cstdio>
#include "request_checks.h"
#include "scheduler_checks.h"

int main() {
  if (const char *failure = scheduler_checks()) {
    std::fprintf(stderr, "FAIL: %s\n", failure);
    return 1;
  }
  if (const char *failure = request_checks()) {
    std::fprintf(stderr, "FAIL: %s\n", failure);
    return 1;
  }
  std::puts("VOICE_ALARM_NATIVE_PASS");
  return 0;
}
