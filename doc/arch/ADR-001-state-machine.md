# ADR-001: Alarm State Machine & Ticker Model

## Status
**Proposed**

## Context
The alarm clock must operate **offline-first** — it cannot depend on Home Assistant being available or the network being up. The ESP32 must tick its own alarms using the local RTC, independent of any external service.

The ESPHome framework is event-loop-based. It does not use threading or traditional `sleep()` patterns. This creates constraints for how we implement a periodic alarm-tick mechanism.

Additionally, the device must handle **interruption mode**: when an alarm is ringing, a wake-word detection (e.g., "Stop alarm") must be able to interrupt the ring state immediately.

## Decision
We use `esphome::interval` for periodic tick checking at **1 second** (following ESPHome's timer pattern). This aligns with ESPHome's event-loop model and avoids the complexity of raw timer interrupts which can conflict with WiFi/BLE stacks.

For interruption handling, we implement a **priority-based state machine** where wake-word events have higher priority than the ringing state.

### State Machine
```
                    ┌─────────┐
                    │  IDLE   │
                    └────┬────┘
                         │ SetAlarm
                    ┌────▼────┐
                    │  SET    │◄───────────┐
                    └────┬────┘            │ Tick matches, snooze
                         │ Time matches   │ alarm_snoozed
                    ┌────▼────┐    ┌──────▼──────┐
                    │  FIRING │    │  SNOOZED    │
                    │  (1)    │    └─────────────┘
                    └────┬────┘
                         │ Done
                    ┌────▼────┐
                    │  IDLE   │
                    └─────────┘
```

**FIRING Subtypes:**

| Subtype | Trigger | Behavior | Snooze/Wake-Word |
|---------|---------|----------|------------------|
| `ALARM` | Alarm time matched | Plays Buzzer loop (D1) | `WAKE_WORD_DETECTED` or Stop → `SNOOZED` |
| `REMINDER` | Reminder time matched | Plays TTS text once (UR-04) | Ignored (TTS ends naturally) |

**Multi-Alarm Firing (Sequential):**
- When multiple alarms match the same tick (e.g., 08:00, 08:00, 08:00), they fire **sequentially** — one after another.
- First alarm starts firing, completes (or is snoozed/stopped), then the next alarm in the queue begins.
- Alarm order within same-time: oldest-first (FIFO, based on NVS entry ID).
- Priority: `ALARM` subtype fires before `REMINDER` (alarms wake the user first).
- The ticker continues checking remaining alarms after the current FIRING completes.

**Priority handling:**
- Subtype `ALARM` is interruptible: `WAKE_WORD_DETECTED` or button stop → `SNOOZED`
- Subtype `REMINDER` is not interruptible: TTS plays to completion

## Alternatives Considered

| Alternative | Pros | Cons |
|-------------|------|------|
| `esphome::interval` (polling) | Native ESPHome, safe, easy | Must poll frequently enough to avoid missing alarms |
| Hardware Timer Interrupt | Precise, low CPU | Complex, can conflict with WiFi/BLE, not ESPHome-idiomatic |
| ESP32 RTC Interrupts | Deep sleep aware | Only fires on RTC match, not useful for active operation |
| External polling from HA | Simple | Breaks offline-first requirement |

## Consequences

### Positive
- Offline-first guarantee: alarms fire correctly even without network
- Intentionally polling every 1-5 seconds is acceptable for an alarm clock (sub-second precision is unnecessary)
- Wake-word interruption is handled cleanly via priority

### Negative
- Must balance poll interval vs. power consumption: too frequent = extra CPU use, too infrequent = alarm delay up to N seconds
- State machine transitions must be idempotent (same intent during `RINGING` should not cause double-fire)

## Open Questions
*All resolved: 1s poll, immediate fire on NTP-shift, FIRING+subtype, sequential multi-alarm, idempotent ignore.*

These are now resolved:
1. **Poll interval:** ✅ 1 second (per ESPHome timer pattern)
2. **RTC wake from deep sleep:** ✅ Not in v1 scope, state machine doesn't need to account for it
3. **Race condition during TTS:** ✅ Per D3/C2 — alarm preemption required (ALARM subtype fires before REMINDER subtype)

Additional resolved decisions:
- **NTP-Shift-Verhalten:** Alarm fires immediately if it was in SET and time shifted past the alarm time
- **Multi-Alarm Firing:** Sequential (oldest-first FIFO), ALARM before REMINDER
- **Idempotency:** Duplicate `SetAlarm` during FIRING is ignored

## References
- PRD §2: Goals & Scope
- PRD D1, D2, D3 (Standard tone, snooze, preemption)
- PRD F2 (State machine with interruption mode)
- PRD NFR-01 (Reliability: ±1 second tolerance)