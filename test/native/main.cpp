#include <cstdio>
#include "scheduler_checks.h"

int main() {
  if (const char *failure = scheduler_checks()) {
    std::fprintf(stderr, "FAIL: %s\n", failure);
    return 1;
  }
  std::puts("VOICE_ALARM_NATIVE_PASS");
  return 0;
}
