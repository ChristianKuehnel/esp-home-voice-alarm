# v1 Implementation Plan

**Date:** 2026-10-13
**Baseline:** [ADR-009](arch/ADR-009-v1-implementation-baseline.md)
**Status:** Ready to start implementation; all milestones below remain pending.

The owner has a Voice PE and working HA Assist available. First establish a working build and communication path, then prove an alarm can be set, persisted, fired, and dismissed offline. Add German and English voice commands before calling v1 complete.

## 1. Establish a reproducible baseline

- Pin compatible ESPHome, HA/Python development dependencies, and an upstream Voice PE package revision. Record tested versions and clean-environment commands. The existing HA `2024.1.0` metadata is not verified compatibility evidence.
- Repair ESPHome imports/schema/code generation, lifecycle methods, header dependencies, logging, and initialization. Instantiate/connect the scheduler. Replace raw speaker/button GPIO assumptions with component references and supported YAML hooks.
- Repair HA config-flow `self` usage, manifest registration, entity lifecycle, and options handling. Validate the existing ESPHome device binding and prevent duplicates. Use firmware-backed volume rather than a separate HA value.
- Fix the external-component path in `test/voice-pe.yaml` to resolve from the YAML directory (`../components`). Provide a minimal compilation fixture and a real Voice PE package recipe with network, clock, audio, and wake-word bindings. Disable disconnection-triggered reboot timers in the latter. Keep real credentials out of fixtures.
- Prove a targeted HA action, correlated device acknowledgment, and readable state snapshot. Record the exact wire mapping and schema version; reject incompatible firmware.
- Add Python checks and firmware builds to CI. The current scheduled workflow tests stable/beta despite its dev comment: add an actual scheduled dev job and a pinned required baseline.

**Exit:** Clean-environment validation/compilation and HA setup/unload tests pass; the request/result round trip works against the selected versions. Document commands reproducing CI.

## 2. Implement scheduling and persistence

- Build a scheduler with injected wall and monotonic clocks, independent of audio/network calls, so edge cases run without hardware.
- Implement the ADR-009 model, Monday-first mask, stable IDs, eight-entry default, validation, and versioned storage codec.
- Implement time-validity gating, crossed deadlines, inclusive five-minute catch-up, DST handling, repeating rearming, occurrence deduplication, shared sessions, and 15-minute timeout.
- Implement durable acknowledgments and storage/occurrence failure handling. Define and test the boundary between recording an occurrence and starting playback, including sudden resets.

**Exit:** Scheduling/storage acceptance cases pass, including corruption, failed saves, and restarts. Stopping a repeating alarm retains its next occurrence.

## 3. Prove offline alarms on Voice PE

- Play a locally available timer tone through upstream audio; no runtime network fetch for ringing.
- Apply persisted independent alarm volume, preempt media, restore prior volume afterward, and bypass DND suppression.
- Connect the existing center button and local wake word to immediate dismissal. Preserve ordinary behavior outside ringing and respect microphone mute.
- Expose state/firing/volume entities and a device-side set action for the end-to-end check.

**Exit:** Set and acknowledge an alarm, disconnect HA/Wi-Fi, hear it ring, and dismiss by button and wake word in separate trials. Remain offline beyond the normal disconnection timeout without reboot. Verify automatic timeout and overlap handling. Reboot offline, observe `waiting_for_time`, then reconnect to verify catch-up.

## 4. Complete HA services and synchronization

- Implement `voice_alarm.set_alarm`, `delete_alarm`, `stop`, and `list_alarms` with schemas and service descriptions.
- Implement targeting, IDs/revisions, request correlation, stale-write rejection, acknowledgment timeouts, and useful errors.
- Refresh authoritative device snapshots on reconnect; emit live per-occurrence events without replaying stale events.
- Test multiple devices, unload/reload, unavailable devices, timeouts, persistence failures, and reconnects.

**Exit:** Only the selected device changes/rings. HA never confirms an unacknowledged save or overwrites a device schedule with stale cache. State converges after reconnect.

## 5. Add German and English voice commands

- Implement HA intents and sentence definitions for setting, explicit dates, recurrence, and cancellation through the same services used by automations.
- Resolve bare hours using 24-hour/next-occurrence semantics; honor explicit qualifiers and reject invalid explicit dates/times.
- Confirm the device-acknowledged result, including resolved time/date or recurrence, in the active pipeline language. Handle errors in both languages.
- Keep immediate wake-word dismissal entirely local; speech parsing is not on its critical path.

**Exit:** Both language test sets pass; the owner verifies spoken setting, cancellation, confirmation, and offline dismissal on Voice PE. No cloud voice service is required by the implementation.

## 6. Validate and document delivery

- Write installation/setup instructions for the tested Voice PE recipe and HA integration, existing Assist prerequisites, timezone alignment, services, and offline/reboot behavior.
- Validate HACS metadata and the release ZIP layout against a clean install. Existing packaging scaffolding is not installation proof.
- Record hardware results and tested versions for offline duration/drift, audible start latency, volume/audibility, wake-word recognition during ringing, and persistence/reboot cases.
- Check results against PRD targets; report failed targets with evidence rather than silently weakening requirements.

**Exit:** Clean installation succeeds, required CI passes, and hardware acceptance has recorded results. Deferred features do not block v1.

## Acceptance cases

| Case | Expected result |
|------|-----------------|
| Bare hour | At 06:00, “seven” means today 07:00; at 08:00, tomorrow 07:00. Same rule in both languages. |
| Explicit date/qualifier | “Tomorrow at seven” / “morgen um sieben” means tomorrow 07:00; “seven in the evening” means 19:00. Explicit past times are rejected. |
| Weekdays/one-shot | Mask `31` selects Monday–Friday on both sides. Mask `0` fires once and releases its entry. |
| Save, reboot, no time | Schedules/volume survive; firing waits for valid synchronization. |
| Synchronization at due + 299 / 300 / 301 seconds | Ring / ring / skip. Future repeating occurrences remain. |
| Delayed tick | Crossing a deadline without observing second zero still fires within the catch-up policy. |
| Clock corrections | Forward shifts follow catch-up; backward shifts do not repeat handled occurrences. |
| Spring DST | A recurring 02:30 alarm is skipped where that time does not exist; no substitute 03:00 firing. |
| Autumn DST | At most one firing across the repeated hour, including a reboot after the first occurrence. |
| Overlap | Simultaneous/newly due alarms share the session. One dismissal consumes all already-due occurrences and preserves future ones. |
| Timeout | Stop at 15 minutes of monotonic elapsed time, unaffected by clock changes or later arrivals. |
| Stop/delete | Idle stop preserves schedules. Delete requires an ID or explicit all flag; malformed requests remove nothing. |
| Offline dismissal | Button and wake word each work without HA. A muted microphone does not prevent button dismissal. |
| Media/volume | Saved alarm volume applies regardless of media volume/DND; prior media volume returns afterward. |
| Capacity/IDs | Ninth create at default capacity fails without modifying existing alarms. Deletion does not renumber IDs. |
| Storage failure | Corrupt/unknown formats and incompatible capacity changes preserve data and surface an error. Test round trips and interrupted saves. |
| Save/acknowledgment failure | Never speak success before durable acknowledgment. Refresh after timeout; no blind duplicate create. |
| Multiple clients/devices | Serialize valid mutations, reject stale revisions, and affect only the intended device. Voice targets its originating device. |
| Offline fire/reconnect | Refresh authoritative state; do not replay missed live HA firing events. |

Test observable behavior instead of mirroring implementation. Hardware checks supplement the deterministic suite.
