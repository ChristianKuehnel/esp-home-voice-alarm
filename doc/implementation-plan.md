# v1 Implementation Plan

**Date:** 2026-10-13 (updated 2026-10-19: M1.1–M1.5 and M2.1–M2.5 complete; code-state review 2026-10-18)
**Baseline:** [ADR-009](arch/ADR-009-v1-implementation-baseline.md)
**Scope:** Strict v1 per ADR-009 — alarms only on Voice PE. Reminders, snooze, Satellite1, LLM/MCP, music/custom tones, fade-in, named alarms, and voice listing are explicitly out of scope.

## Consistency review (2026-10-18)

PRD, ADR-009, README, and this plan are mutually consistent on every v1 behavior (offline ringing, reboot + 300 s catch-up, weekday masks, eight entries, volume 0.1–1.0/step 0.01/default 0.7, 15-min monotonic ringing timeout, shared session, entities/live events, no DND suppression). Remaining inconsistencies are code-vs-doc drift, tracked as tasks below:

| Drift | Location | Fixed by |
|-------|----------|----------|
| `tm_sec == 0` firing; single entry; one-shot mask `0` never satisfiable | `components/voice_alarm/alarm_state.cpp:78-80` | M2.1–M2.3 |
| No persistence; `handle_alarms()` empty stub | `components/voice_alarm/alarm_state.cpp:42,91-97` | M2.4, M3.1 |
| HA-local volume entity, step 0.1, never touches device (forbidden by ADR-009:72) | `custom_components/voice_alarm/alarm_clock.py:16,31-40` | M1.1 |
| Config flow: no `unique_id`, module-level `async_step_user`, dead `OptionsFlow` | `custom_components/voice_alarm/config_flow.py:25,45-63` | M1.1 |
| `manifest.json` claims `local_polling` | `custom_components/voice_alarm/manifest.json:9` | M1.1 |
| HA `2024.1.0` claim unverified | `hacs.json:4` | M1.2 |
| No services, intents, entities, or event handling | `custom_components/` | M4.x, M5.x |

## Current state

| Area | Status |
|------|--------|
| Test infrastructure (native C++, host API, esp-emu NVS double-boot) | **Done** — all three layers pass; CI runs them plus scheduled compatibility builds |
| `components/voice_alarm/` — scheduler | **M2 done** — ADR-009 model: eight stable-ID entries (repeating + one-shot), crossed-deadline firing with inclusive 300 s catch-up, DST gap/repeated-hour handling, shared ringing session with 15-min monotonic timeout, `waiting_for_time`/`idle`/`armed`/`ringing`/`error` states; proven in the native layer |
| `components/voice_alarm/` — persistence | **M2 done** — versioned `vasc` schedule blob (magic/version/count/fields/allocator), durable save on schedule changes and occurrence transitions, corrupt/unknown/incompatible blobs preserved + error surfaced; round trips, restart dedup, and storage failure proven in native + host. Audio, volume, entities, and events remain (M3/M4) |
| HA integration | **M1.1 done** — device-registry config flow with `unique_id`, duplicate prevention, registry validation; no HA-local entities (volume is firmware-backed per ADR-009:72); en/de strings; 6-test pytest suite against HA 2026.2.3. Services/intents/events remain for M4.x/M5.x |
| Compatibility matrix | **M1.2 done** — pinned in `doc/versions.md` (ESPHome 2026.9.1 / HA 2026.2.3 / aioesphomeapi 46.3.0 dev + 43.14.0 HA / esp-emu 0.49.0 / Upstream Voice PE package @ `dev` `d4e6fa4`) with clean-environment commands |
| Voice PE recipe | **M1.3 done** — real recipe at `test/voice-pe.yaml`: thin main config over the vendored upstream package (`vendor/home-assistant-voice-pe` @ `d4e6fa4`), adds `voice_alarm` + NTP `time` source, disables disconnect-triggered reboots; compile-verified on the official Voice PE target with ESPHome 2026.9.1 |
| CI | **Done** — native checks, host build/API (incl. `set_alarm` request/ack e2e), S3 build/emulator, the HA integration suite on the pinned matrix, plus scheduled stable/beta/dev compatibility builds |

The owner has a Voice PE and working HA Assist available. Sequence: establish a working build and HA↔device communication path, prove scheduling/persistence deterministically, prove offline alarms on hardware, then complete HA services and both-language voice.

## Milestone 1: Establish a reproducible baseline

**Done:** pinned ESPHome/Python test dependencies (`test/requirements.txt`); native scheduler, host API, and S3 emulator suites with a preferences probe surviving restarts; repaired ESPHome schema/codegen, lifecycle, and header dependencies; host/S3 fixtures; CI with native/host/emulator jobs and scheduled stable/beta/dev compatibility builds.

### Tasks

**M1.1 — Repair the HA integration shell.**
- Validate the ESPHome device binding against the HA device registry during setup; add `unique_id`; prevent duplicate entries.
- Fix config-flow `self` usage; remove or wire the dead `OptionsFlow`.
- Delete the disconnected HA-local volume entity; volume must be firmware-backed (ADR-009:72).
- Add root `strings.json`/`en.json` so the English setup UI renders.
- Fix `manifest.json` (`iot_class`; replace the unverified `2024.1.0` claim with a validated minimum after M1.2 pins versions).
- Add HA integration tests: setup, unload, duplicate prevention, registry validation failure.

*Acceptance:* hassfest and the new pytest suite pass against the pinned HA/`aioesphomeapi` versions; no entity exists that does not reflect firmware state.

**M1.2 — Pin the compatibility matrix.**
- Select and record: ESPHome (already `2026.9.1` in CI), HA/`python`/`aioesphomeapi` development versions, upstream [Voice PE firmware](https://github.com/esphome/home-assistant-voice-pe/blob/dev/home-assistant-voice.yaml) package revision, and the Python toolchain.
- Document clean-environment commands for a fresh checkout (venv, esp-idf/toolchain via `.tools`, emulator install).

*Acceptance:* a clean clone reproduces every CI layer and the recorded versions; the matrix is committed to `test/README.md` or a dedicated `doc/versions.md`.

**M1.3 — Provide a real Voice PE recipe.** (Done 2026-10-19)
- Package with Wi-Fi (disconnection-triggered reboot disabled per ADR-009:82), `time`/NTP, `voice_assistant` with the upstream package's audio, center button, and local wake-word facilities — no new GPIO buzzer, no redefinition of the existing button.
- Bind the `voice_alarm` component and its entities/actions. Real credentials excluded; document where they go.

*Acceptance:* compiles for the official Voice PE target on the pinned ESPHome with the pinned package revision; recipe includes no raw speaker/button GPIO assumptions.

**M1.4 — Prove the request/acknowledgment path.** (Done 2026-10-19)
- One targeted HA→device action on the host fixture (production-style, not a `test_*` action): request with ID + expected revision → device mutates, persists, and correlates the acknowledgment → HA refreshes the authoritative snapshot.
- Record the exact wire mapping, schema version, and the rejection behavior for incompatible firmware.

Implementation: `components/voice_alarm/alarm_request.{h,cpp}` (pure C++ validation, `request_id` echo, revision bump after durable persistence); host device action `set_alarm` with an `api.respond` v1 acknowledgment; `custom_components/voice_alarm/alarm_protocol.py` with typed errors (`UnsupportedFirmwareError`, `StaleRevisionError` carrying the authoritative revision, `VoiceAlarmCommandError`). Native checks, host e2e round trip (accept/stale/invalid + restart keeps revision), and HA-venv protocol unit tests per the matrix in `doc/versions.md`.

*Acceptance:* an end-to-end test passes via the native API against the compiled host binary; incompatible/older firmware is rejected with a clear error.

**M1.5 — Add HA checks to CI.** (Done 2026-10-19)
- Job running the HA pytest suite from M1.1 (setup/unload/config-flow, the request/ack protocol unit tests, and manifest/translation sanity checks) on the pinned matrix (`test/ha/requirements.txt`, Python 3.13).

Note: full `hassfest` is not in CI — it ships only inside a source checkout of `homeassistant/core` (no PyPI release), so it would add an unreviewable second checkout to the job. `test/ha/test_manifest.py` covers the hassfest-flagged fields (domain, version, `iot_class`, dependencies, translation steps) instead; revisit if a source-checkout-based hassfest step is desired.

*Exit:* clean-environment validation/compilation and HA setup/unload tests pass; the M1.4 round trip works against the pinned versions; commands reproducing CI are documented.

## Milestone 2: Implement scheduling and persistence

**Done (2026-10-19):** the scaffold is rewritten into the ADR-009 model. All scheduling logic runs against injected wall + monotonic clocks with no audio or network on the scheduling path. Proven in the native layer (model/validation, firing/catch-up/DST, session/timeout) and the host layer (persistence across restart, restart dedup, storage failure). The native `scheduler_checks`/`dst_checks` and the host suite run in lockstep.

Rewrite the scaffold (`components/voice_alarm/alarm_state.*`) into the ADR-009 model. All logic against injected wall + monotonic clocks; no audio or network calls on the scheduling path. Expand `test/fixtures/scheduler_checks.h` and the host suite in lockstep.

### Tasks

**M2.1 — Data model and validation.**
- Entry: stable device-assigned ID, kind (one-shot with explicit local date/time, or repeating with hour/minute + Monday-first mask), enabled flag, occurrence bookkeeping, ID allocator. Default capacity eight; no renumbering on remove; overflow is an error that leaves existing entries untouched.
- Validation: hour 0–23, minute 0–59, mask 0–127; reject explicit past or nonexistent one-shot times; an ambiguous autumn one-shot resolves to its first occurrence; bare times resolve to the next strictly future occurrence.

*Acceptance:* model/validation checks pass in the native layer, including ninth-create at default capacity and non-renumbering deletes.

**M2.2 — Firing evaluation.**
- One-second crossed-deadline checks against the injected clock (no `tm_sec == 0`); an unobserved crossing still fires within policy.
- Inclusive 300-second catch-up for delayed ticks, forward clock corrections, and boot synchronization; +299 rings, +300 rings, +301 skips.
- DST: spring gap = skipped date (no substitute 03:00 firing); repeated autumn hour rings at most once, including across a reboot, via persisted occurrence bookkeeping.
- Backward clock corrections never refire a handled occurrence.

**M2.3 — Ringing session.**
- Separate schedules from the session; keep evaluating while ringing.
- Newly due alarms join the active session. One dismissal (or timeout) consumes every occurrence already due, removes completed one-shots, and retains repeating schedules for their next occurrence. Idle stop is a no-op; deletion is separate and requires an ID or explicit all.
- 15-minute timeout from monotonic session start, unaffected by clock changes or later arrivals.
- Define and test the persistence boundary: an occurrence is recorded before audio starts, so a sudden reset cannot refire it after reboot; handled occurrences never replay.

*Acceptance:* all session/DST/catch-up rows of the acceptance table pass in the native layer.

**M2.4 — Versioned durable persistence.**
- ESPHome preferences with explicit versioned serialization: validated magic/version, count, per-entry length/fields, allocator. No raw compiler-dependent structs; no obsolete 65-byte blob.
- Writes only on schedule changes and necessary occurrence transitions — never per tick. A durable save includes the preferences flush; an in-memory update is not an acknowledgment.
- Corrupt/unknown formats and incompatible capacity reduction preserve existing data and surface an error; define the migration policy before the first released format changes.
- Test interrupted saves in the emulator double-boot and file-backed host preferences.

*Acceptance:* round trips, corruption, interrupted-save, and failed-write cases pass across native, host, and emulator layers; restart dedup holds.

**M2.5 — Time-validity states.**
- Device states `waiting_for_time`, `idle`, `armed`, `ringing`, `error` with a diagnostic error reason; boot loads schedules, waits for valid synchronization, then applies the catch-up rule.

*Exit:* scheduling/storage acceptance cases pass, including corruption, failed saves, and restarts; stopping a repeating alarm retains its next occurrence.

## Milestone 3: Prove offline alarms on Voice PE

Uses the M1.3 recipe. Deterministic equivalents live in the test layers; the hardware gates HA-1…HA-5 below are **blocking, owner-run tasks** with pass/fail evidence recorded per ADR-009 ("Verification still needed").

### Tasks

**M3.1 — Local audio and volume.**
- Play the locally available timer tone through the upstream audio pipeline; no runtime network fetch for ringing.
- Apply the persisted independent alarm volume (step 0.01, 0.1–1.0), preempt media, and restore the prior media volume after ringing. Bypass DND suppression.

**M3.2 — Physical and voice dismissal.**
- Center button and local wake-word callbacks dismiss immediately without waiting for HA; the microphone-mute state does not prevent button dismissal. Ordinary Assist/button behavior is preserved outside ringing.

**M3.3 — Entities and device-side action.**
- Expose firmware-backed state, firing status, and volume as standard native-API entities, plus the device-side set action used by M1.4/M4.

### Owner hardware gates (blocking)

| Gate | Trial |
|------|-------|
| **HA-1** | Set and durably acknowledge an alarm; disconnect HA and Wi-Fi; the alarm rings audibly with sound start ≤ 2 s (NFR-02). |
| **HA-2** | Separate trials: dismiss by button, and dismiss by wake word while ringing, both with HA unreachable; audio stops, state converges to `idle`. |
| **HA-3** | Remain offline beyond the normal disconnection timeout; the device does not reboot and ringing/dismissal keep working. |
| **HA-4** | Verify the automatic 15-minute stop and that a second alarm becoming due joins the same session; one dismissal consumes both due occurrences. |
| **HA-5** | Reboot while offline: observe `waiting_for_time`; reconnect and verify the inclusive 300-second catch-up behavior for the missed occurrence. |

*Exit:* HA-1…HA-5 all pass with recorded evidence; deterministic suite still green.

## Milestone 4: Complete HA services and synchronization

**M4.1 — Services.** Implement `voice_alarm.set_alarm`, `delete_alarm`, `stop`, and `list_alarms` with schemas and service descriptions per the ADR-009 contract (exactly one target device; set validates hour/minute/mask/date and optional ID; delete requires ID or explicit `all`; stop is idempotent when idle; list returns the authoritative snapshot or reports unavailability).

**M4.2 — Request correlation.** Mutating requests carry a request ID and expected revision; the device serializes mutations, rejects stale revisions, and correlates acknowledgments. On acknowledgment timeout: refresh authoritative state and report uncertainty — never a blind duplicate create, never a success claim without durable acknowledgment.

**M4.3 — Snapshot and events.** Reconnect refreshes a full authoritative device snapshot without replacing it with stale HA cache. Emit `esphome.voice_alarm_fired` live, once per occurrence (including occurrences joining an existing session), with device identity, alarm ID, occurrence, and session ID. No replay of missed events after offline firing.

**M4.4 — Multi-device and lifecycle tests.** Voice requests target the originating device; other callers select exactly one device explicitly (no broadcast, no guessing). Test: multiple devices, unload/reload, unavailable devices, acknowledgment timeouts, persistence failures, and reconnects.

*Exit:* only the selected device changes/rings; HA never confirms an unacknowledged save or writes a stale cache over a device schedule; state converges after reconnect.

## Milestone 5: Add German and English voice commands

**M5.1 — Intents and sentences.** HA intent handlers plus German and English sentence definitions covering: setting one-shots, explicit dates (tomorrow/morgen), recurrence, and cancellation — all routed through the M4 services used by automations. Use supported upstream hooks (not ADR-006's speculative `on_intent` example).

**M5.2 — Time resolution.** Bare hours use 24-hour time and the next strictly future occurrence; explicit dates and morning/evening qualifiers override; invalid or explicit past dates/times are rejected with an error in the active pipeline language.

**M5.3 — Confirmations.** Confirm only the device-acknowledged result, including resolved time/date or recurrence, in the Assist pipeline language. No success speech before durable acknowledgment.

**M5.4 — Local dismissal unchanged.** Wake-word dismissal stays fully local; speech parsing is not on its critical path.

**T5.1 — Language test sets.** Deterministic tests for both language sets (sentence parsing, resolution rules, rejection paths) plus one integration pass against the host binary where feasible.

### Owner hardware gate (blocking)

| Gate | Trial |
|------|-------|
| **HA-6** | On the Voice PE: spoken setting (both languages), spoken cancellation, and the verbal confirmation each verified once; offline wake-word dismissal re-verified during this milestone. No cloud voice service is used. |

*Exit:* both language test sets pass; HA-6 passes with recorded evidence.

## Milestone 6: Validate and document delivery

**M6.1 — Installation documentation.** Setup for the tested Voice PE recipe and the HA integration (HACS/custom components zip), Assist prerequisites, device-registry binding, timezone alignment between HA and device, service reference, and offline/reboot behavior.

**M6.2 — Packaging.** Validate HACS metadata and the release ZIP layout (`voice_alarm.zip`) against a clean install. Existing scaffolding is not installation proof — perform the clean-install trial.

**M6.3 — Record hardware measurements.** Offline duration/drift (PRD target ≤ 1 s/24 h — unverified), audible sound-start latency (NFR-02), volume/audibility (NFR-06 ≥ 70 dB at 1 m — unverified), wake-word recognition quality during ringing, and persistence/reboot cases.

**M6.4 — Requirement reconciliation.** Compare results against PRD targets; failed targets are reported with evidence and a proposed requirement/hardware change before release compliance is claimed. Never silently weaken a requirement.

*Exit:* clean installation succeeds, required CI (ESPHome layers + HA checks) passes, and all hardware acceptance cases have recorded results. Deferred features do not block v1.

## Acceptance cases

| Case | Expected result | Proven by |
|------|-----------------|-----------|
| Bare hour | At 06:00, "seven" means today 07:00; at 08:00, tomorrow 07:00. Same rule in both languages | native (M2), T5.1, HA-6 |
| Explicit date/qualifier | "Tomorrow at seven" / "morgen um sieben" → tomorrow 07:00; "seven in the evening" → 19:00; explicit past times rejected | T5.1, native (M2) |
| Weekdays/one-shot | Mask `31` = Monday–Friday on both sides; mask `0` fires once and releases its entry | native (M2.1/M2.3) |
| Save, reboot, no time | Schedules/volume survive; firing waits for valid synchronization | emulator, host, HA-5 |
| Sync at due + 299 / 300 / 301 s | Ring / ring / skip; future repeating occurrences remain | native (M2.2) |
| Delayed tick | Crossing a deadline without observing second zero still fires within catch-up policy | native (M2.2) |
| Clock corrections | Forward shifts follow catch-up; backward shifts do not repeat handled occurrences | native (M2.2) |
| Spring DST | Recurring 02:30 skipped where the time does not exist; no substitute 03:00 firing | native (M2.2) |
| Autumn DST | At most one firing across the repeated hour, including a reboot after the first occurrence | native (M2.2), M2.4 restart tests |
| Overlap | Simultaneous/newly due alarms share the session; one dismissal consumes all already-due occurrences and preserves future ones | native (M2.3), HA-4 |
| Timeout | Stop at 15 minutes of monotonic elapsed time, unaffected by clock changes or later arrivals | native (M2.3), HA-4 |
| Stop/delete | Idle stop preserves schedules; delete requires an ID or explicit `all`; malformed requests remove nothing | native (M2.3), M4.1 tests |
| Offline dismissal | Button and wake word each work without HA; a muted microphone does not prevent button dismissal | host (`test_button`/`test_wake_word`), HA-2 |
| Media/volume | Saved alarm volume applies regardless of media volume/DND; prior media volume returns afterward | M3.1 + HA-1 |
| Capacity/IDs | Ninth create at default capacity fails without modifying existing alarms; deletion does not renumber IDs | native (M2.1) |
| Storage failure | Corrupt/unknown formats and incompatible capacity changes preserve data and surface an error; round trips and interrupted saves tested | native, emulator, host (M2.4) |
| Save/acknowledgment failure | Never report success before durable acknowledgment; refresh after timeout; no blind duplicate create | host (M1.4, M4.2) |
| Multiple clients/devices | Serialize valid mutations, reject stale revisions, affect only the intended device; voice targets its originating device | M4.4 tests |
| Offline fire/reconnect | Refresh authoritative state; do not replay missed live HA firing events | M4.3 tests, HA-5 |

Test observable behavior instead of mirroring implementation. Hardware checks (HA-1…HA-6) supplement the deterministic suite.

## Out of scope (v1)

Reminders, snooze, music/custom tones, per-alarm sound selection, fade-in, named alarms, voice listing, duration-based reminders, LLM/MCP support, Satellite1, cross-device sync, displays, timers (Voice PE already provides them), dashboards, battery-backed RTC, and offline time recovery after reboot. See PRD §3.4 and the version roadmap.
