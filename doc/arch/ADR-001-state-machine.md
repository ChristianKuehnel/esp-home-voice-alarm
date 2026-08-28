# ADR-001: Alarm State Machine & Ticker Model

## Status
**Proposed**

## Context
The alarm clock must operate **offline-first** — it cannot depend on Home Assistant being available or the network being up. The ESP32 must tick its own alarms using the local RTC, independent of any external service.

The ESPHome framework is event-loop-based. It does not use threading or traditional `sleep()` patterns. This creates constraints for how we implement a periodic alarm-tick mechanism.

Additionally, the device must handle **interruption mode**: when an alarm is ringing, a wake-word detection (e.g., "Stop alarm") must be able to interrupt the ring state immediately.

## Decision
We use `esphome::interval` for periodic tick checking (every 1-5 seconds) rather than hardware timer interrupts. This aligns with ESPHome's event-loop model and avoids the complexity of raw timer interrupts which can conflict with WiFi/BLE stacks.

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
                    │  RINGING│    │  SNOOZED    │
                    └────┬────┘    └─────────────┘
                         │ Alarm fired
                    ┌────▼────┐
                    │  FIRING │
                    └────┬────┘
                         │ Done
                    ┌────▼────┐
                    │  DONE   │
                    └─────────┘
```

**Priority handling:**
- In `RINGING` state: any `WAKE_WORD_DETECTED` → `SNOOZED` (via `alarm_clock.cancel_alarm` or `alarm_clock.snooze`)
- `RINGING` is preemptable, `FIRING` is not (once the TTS plays, it completes)

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
1. **Poll interval:** 1 second? 5 seconds? 1 second is safest but burns more cycles. 5 seconds means worst-case alarm delay is 4.999s (acceptable for an alarm clock).
2. **RTC wake from deep sleep:** Should the alarm clock support waking from deep sleep to fire an alarm? This would require RTC timer + deep sleep transitions. Not in v1 scope, but should the state machine account for it?
3. **Race condition during TTS:** If the device is playing TTS for a reminder and an alarm fires, does the alarm preempt the TTS? (Answer: yes, per D3/C2 — alarm preemption is required.)

## References
- PRD §2: Goals & Scope
- PRD D1, D2, D3 (Standard tone, snooze, preemption)
- PRD F2 (State machine with interruption mode)
- PRD NFR-01 (Reliability: ±1 second tolerance)