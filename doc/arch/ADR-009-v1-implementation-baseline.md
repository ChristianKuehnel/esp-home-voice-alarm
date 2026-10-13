# ADR-009: v1 Implementation Baseline

**Status:** Accepted product decisions; implementation defaults identified below
**Date:** 2026-10-13
**Source:** Repository review and owner interview
**Supersedes:** ADR-001 through ADR-006 for v1. ADR-007 and ADR-008 retain their distribution and CI direction, subject to the implementation plan's validation tasks.

## Owner decisions

| Topic | Agreed behavior |
|-------|----------------|
| Hardware | Home Assistant Voice PE first. The owner has a device and a working Home Assistant Assist setup available for testing. |
| First release | Alarms only: voice/HA creation, one-time and repeating schedules, local tone, independent volume, physical and voice dismissal. Reminders follow later. |
| Offline operation | Already-set alarms continue during HA/Wi-Fi outages while the device stays powered and has synchronized time. |
| Reboot | Load saved schedules, then wait for valid time synchronization before evaluating them. No battery-backed RTC or offline time recovery after reboot is required for v1. |
| Voice dismissal | A locally detected wake word immediately dismisses a ringing alarm, including without HA. No subsequent spoken stop command is required. |
| Missed occurrence | When time synchronization returns after reboot, ring occurrences at most five minutes late; skip older occurrences. |
| DST | Repeating alarms retain their local wall-clock time. Skip nonexistent spring times; ring once when a time repeats in autumn. |
| Ringing limit | Stop automatically after 15 minutes. |
| Overlapping alarms | Alarms becoming due during ringing join the current session. One dismissal stops all occurrences already due; future occurrences remain scheduled. |
| Ambiguous spoken time | Interpret bare hours as 24-hour time and use the next occurrence. Explicit dates and morning/evening qualifiers override the default. |
| Languages | German and English commands and confirmations, using the HA Assist pipeline's language. |

## Scope and architecture

The ESP32 owns schedules, occurrence tracking, persistence, firing, and dismissal. HA owns service validation, voice parsing, device routing, and online confirmations. The `voice_alarm` custom HA integration and ESPHome external component ship from this repository. Exposing standard ESPHome entities does not by itself implement the HA alarm services.

Use the existing Voice PE package's audio, physical button, and local wake-word facilities. The hardware adapter must not create a GPIO buzzer or redefine the existing button. Ordinary Assist/button behavior remains available outside ringing. Alarms preempt media and are not suppressed by DND. Microphone mute remains respected, so the physical button must always provide dismissal independently of voice.

The first release includes HA set/delete/stop operations, a programmatic alarm list for management and synchronization, alarm state, volume, and live firing events. Voice setting and cancellation use HA intents in both languages. Voice listing, reminders, snooze, music/custom tones, fade-in, named alarms, custom dashboards, Satellite1, and dedicated LLM/MCP support remain later work.

## Implementation defaults

These choices resolve technical ambiguities from the review. They were not separate owner interview answers and can be refined during implementation without changing the agreed behavior above.

### Scheduling

- Align the device's configured timezone with HA during setup; validate the alignment.
- Use one weekday convention everywhere: bit 0 = Monday, bit 6 = Sunday; weekdays = `31`, daily = `127`, one-shot = `0`.
- Store an explicit local date/time for one-shots and hour/minute/weekday mask for repeating schedules. HA resolves relative dates; firmware validates the result.
- A bare time means its next strictly future occurrence. Reject explicit past or nonexistent one-shot times; select the first occurrence of an ambiguous autumn one-shot time. Include the resolved date/time in confirmations.
- Evaluate crossed deadlines every second against an injected clock, not `tm_sec == 0`. Apply the inclusive 300-second catch-up window to delayed ticks and forward clock corrections as well as boot synchronization. A spring DST gap is a skipped date, not a missed occurrence to catch up.
- Persist occurrence identity/bookkeeping to prevent a backward correction, repeated autumn hour, or reboot from firing the same handled occurrence twice. Do not write flash on every tick.
- Separate schedules from the ringing session; continue evaluating schedules while ringing. The 15-minute timeout uses monotonic elapsed time from session start and is not extended by later arrivals or wall-clock corrections.
- Include occurrences due at the dismissal instant in the session being closed. Stop/timeout consume that session's occurrences, remove completed one-shots, and retain repeating schedules for their next occurrence. Idle stop is a no-op; deletion is separate.
- Boot does not restore an active audio session. Handled occurrences do not replay; unhandled occurrences follow the catch-up rule once time is valid. Define and test the persistence boundary around starting playback and sudden resets.

### Storage and volume

- Retain the default capacity of eight alarm entries. Volume storage does not consume an alarm entry. Capacity overflow returns an error without overwriting existing alarms.
- Persist stable device-assigned IDs, enabled state, schedule, occurrence bookkeeping, and the ID allocator. Removing an entry does not renumber other IDs.
- Use ESPHome preferences with an explicitly versioned serialization, validated count/length/fields, and corruption detection. Do not persist raw compiler-dependent struct layouts or implement the obsolete 65-byte blob.
- Write on schedule changes and necessary occurrence transitions. Set/delete/volume success requires a durable save, including the relevant preferences flush. Test failed writes and interrupted saves; an in-memory update is not a durable acknowledgment.
- Unknown/corrupt storage or incompatible capacity reduction must preserve existing data and surface an error. Define migration before changing the first released format.
- Retain volume range `0.1–1.0`, default `0.7`, step `0.01`. Persist it independently of media volume and restore prior media volume after ringing. Automatic resumption of preempted media is deferred.

### HA and firmware interface

Use standard ESPHome native API actions/entities, with no custom native-protocol extension or MQTT dependency. Prove the request/acknowledgment path against pinned versions before implementing the full service layer. A correlated event may carry an acknowledgment if the chosen action interface cannot return it directly.

| HA action | Contract |
|-----------|----------|
| `voice_alarm.set_alarm` | Exactly one target device; hour `0–23`, minute `0–59`, mask `0–127`, and date for one-shots. Omitted alarm ID creates; supplied ID updates an existing entry. Enabled defaults to true. Return ID and authoritative revision after persistence. |
| `voice_alarm.delete_alarm` | Exactly one target device and an alarm ID, or explicit `all: true`. Omitting both is an error. |
| `voice_alarm.stop` | Dismiss the target device's current session; preserve future schedules. Idempotent when idle. |
| `voice_alarm.list_alarms` | Return authoritative schedules/revision; report unavailable rather than presenting a stale snapshot as current. |

Mutating requests carry a request ID and expected revision. The device serializes mutations, rejects stale revisions, and correlates acknowledgments. On acknowledgment timeout, refresh authoritative state and report uncertainty; never blindly repeat a create with a new identity or claim success. Validate malformed requests, unknown IDs, capacity, availability, and storage errors.

Voice requests target the originating Voice PE. Other callers select exactly one device explicitly; never broadcast or guess an ambiguous target. Integration setup validates the HA device-registry binding.

Expose firmware-backed alarm state, firing status, and volume as standard entities. Device states are `waiting_for_time`, `idle`, `armed`, `ringing`, and `error`, with a diagnostic error reason. Do not maintain a disconnected HA-only volume entity.

Use the live event `esphome.voice_alarm_fired`, containing device identity, alarm ID, occurrence, and session ID. Emit once per occurrence, including occurrences joining an existing session. Offline firing proceeds locally; missed HA automation events are not replayed. Reconnect refreshes a full device snapshot without replacing it with a stale HA cache.

### Voice and hardware

Button and local wake-word callbacks dismiss audio without waiting for HA. Online confirmations of schedule changes occur only after durable device acknowledgment, in the Assist pipeline's language. Offline dismissal requires no TTS; a normal online Assist session may continue after dismissal.

Use supported upstream hooks rather than the speculative `voice_assistant.on_intent` example in ADR-006. Both sentence sets cover setting one-shots, explicit dates such as tomorrow, recurrence, and cancellation. Errors use the active pipeline language.

Disable Wi-Fi/API disconnection-triggered reboots in the alarm recipe so an ordinary outage does not discard a valid clock. Retain crash recovery; an actual reboot waits for time synchronization.

## Verification still needed

No further product decision is needed to start [the implementation plan](../implementation-plan.md). Build compatibility, the HA/device acknowledgment path, stock-device drift, audio latency/audibility, and wake-word detection while ringing still need verification. The PRD's one-second-per-day drift and 70 dB targets are unverified, not guarantees established by this interview. Report failed measurements and a proposed requirement/hardware change before claiming release compliance.

## Platform references

- [Official Voice PE firmware](https://github.com/esphome/home-assistant-voice-pe/blob/dev/home-assistant-voice.yaml): hardware/package reference; pin a tested revision during implementation.
- [ESPHome Voice Assistant](https://esphome.io/components/voice_assistant/): supported pipeline hooks.
- [ESPHome native API](https://esphome.io/components/api/): actions, events, and connection timeouts.
- [ESPHome Wi-Fi](https://esphome.io/components/wifi/): outage/reboot configuration.
- [ESPHome time](https://esphome.io/components/time/): clock validity, synchronization, and timezone configuration.
