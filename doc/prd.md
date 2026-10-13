# Product Requirements Document — Alarm Clock

> Reference: Amazon Alexa alarm features + Home Assistant Community Feedback
>
> **Last updated:** 2026-10-13
>
> **v1 baseline:** [ADR-009](arch/ADR-009-v1-implementation-baseline.md) records the owner interview and supersedes the earlier design decisions. Follow the [implementation plan](implementation-plan.md) for delivery order and acceptance cases. Historical feature references below include deferred work; the version roadmap defines release scope.

---

## 1. Vision

Build a voice-controlled alarm clock running on **ESPHome** + **Home Assistant** that delivers the same core alarm experience as **Amazon Alexa** — without requiring a cloud-based voice assistant or proprietary hardware.

The alarm runs **independently on the ESP32 device**. Once set and time-synchronized, it continues through Home Assistant and Wi-Fi outages while the device remains powered. Alarm schedules persist in NVS. After a device reboot, firing waits for valid time synchronization: occurrences at most five minutes late ring, and older occurrences are skipped. Reminders are deferred beyond v1.

The first implementation targets existing Home Assistant Voice PE hardware. The owner has a device and a working Assist setup available for testing.

| Device | URL | v1 role |
|--------|-----|-----------|
| **Home Assistant Voice PE** | [home-assistant.io/voice-pe](https://www.home-assistant.io/voice-pe/) | First target; reuse upstream audio, center button, and local wake-word facilities |
| **FutureProofHomes Satellite1** | [futureproofhomes.net](https://futureproofhomes.net/products/satellite1-smart-speaker) | Deferred; validate a separate device recipe later |

Keep the scheduler independent of hardware. Device-specific ESPHome recipes bind existing components to that core; do not implement the old GPIO-buzzer assumptions.

---

## 2. Background

### 2.1 Alexa Feature Reference

The following features from Amazon's Alexa alarm implementation (Echo, Echo Dot, Echo Spot) form the baseline target for this project. Sections 2.2–2.7 below identify which ones are **in scope** and which are **out of scope**.

**Alarm Creation & Management**

| Feature | Description |
|---------|-------------|
| Single alarm | Set one-time alarm at a specific time |
| Repeating alarm | Set alarm to repeat on selected days |
| Named alarm | Label alarms for easy management |
| AM/PM disambiguation | Alexa prompts or defaults if only hour is given |
| Alarm listing | Query all currently set alarms |
| Alarm editing | Modify an existing alarm's time, sound, or days |
| Alarm deletion | Cancel a specific alarm or all alarms |
| Multiple alarms | Support multiple independent alarms simultaneously |

**Snooze** — Pause the alarm for a configurable interval (default ~9 min) via voice or button (target: v3).

**Alarm Sounds** — Built-in tones, music alarm (HA media player), sound selection per alarm.

**Volume & Wake Behavior** — Independent alarm volume, ascending volume (fade-in), silence-after timer.

**Alarm Dismissal** — Stop (silently dismiss), cancel (delete); snooze is a v3 feature.

**Offline Behavior** — Alarm fires without Wi-Fi using local RTC; default tone fallback for cloud-dependent sounds; offline stop works.

**Smart Home Routines** — Alarm-triggered automations (lights, thermostat, news).

**Physical Hardware** — Clock display, night mode, speaker hardware, button layout.

**Timers** — Count-down timers with listing, naming, and stop.

### 2.2 Technical Foundations — What Exists in ESPHome & HA

This project builds on extensively battle-tested ESPHome and Home Assistant infrastructure. The table below shows what we **reuse** versus what we **implement**:

| Layer | Component | Status | Usage in our project |
|-------|-----------|--------|---------------------|
| **Voice Pipeline** | ESPHome `voice_assistant` / HA Assist | ✅ Existing | Reuse the pipeline; implement alarm setting/cancellation intents in HA. Local wake-word dismissal bypasses HA. |
| **Timer behavior** | Voice PE firmware | ✅ Existing reference | Reuse the package's audio/button facilities where suitable; persistent wall-clock scheduling is implemented here. |
| **ESPHome Native API** | aioesphomeapi | ✅ Existing | Standard entity communication (BinarySensor, Number, Text, etc.) between HA and ESP32. No custom protocols. |
| **NVS / Preferences** | ESPHome preferences | ✅ Existing | Persist alarm schedules, occurrence bookkeeping, and volume using a versioned format. |
| **NTP Client** | ESPHome NTP component | ✅ Existing | RTC synchronization for long-term accuracy. |
| **I2S Audio** | ESPHome `i2s_audio` + XMOS pipeline | ✅ Existing | Audio output to speaker. Voice PE already uses XMOS XU316 I2S pipeline. |
| **Binary Sensor** | ESPHome `binary_sensor` | ✅ Existing | Bind to the upstream device's existing center-button handling. |
| **Media Player** | ESPHome `media_player` component | ✅ Existing | Volume control and music alarm playback (v2). |
| **ESPHome Config Schema** | Native YAML config system | ✅ Existing | Device-specific config files for Voice PE and Satellite1. |
| **HA Assist Intents** | HA intent framework | ✅ Existing | Register this integration's alarm intent handlers and German/English sentences. |

**What we implement (not existing):**

| Component | Where | What it does |
|-----------|-------|-------------|
| **Alarm State Machine** | ESP32 | Offline-first: alarm ticks independently on ESP32 RTC, not HA-driven. |
| **NVS Alarm Storage** | ESP32 | Versioned alarm schedules, stable IDs, occurrence bookkeeping, and volume. |
| **Local RTC Timer Tick** | ESP32 | Periodic check (e.g. every second) against local RTC, not HA polling. |
| **Custom ESPHome Component** | ESP32 | Scheduler, ringing session, and standard state/firing/volume entities. |
| **Intent/Parsing Layer** | Home Assistant | German and English alarm intents, 24-hour/next-occurrence parsing, originating-device routing, and acknowledged confirmations. |
| **HA Service Layer** | Home Assistant | `voice_alarm.set_alarm`, `delete_alarm`, `stop`, and `list_alarms` via the ESPHome native API. Dedicated LLM/MCP support is deferred. |

---

## 3. Requirements

Requirements are categorized by their origin:

| Tag | Origin |
|-----|--------|
| **[A X.X]** | Alexa feature from §2.1 reference |
| **[R-X]** | Community-driven requirement (see RCM table below) |
| **[UR-X]** | User requirement from project owner (see UR table below) |
| **[NFR-X]** | Non-functional requirement (see §4) |
| **[C #XXXXX]** | Home Assistant community discussion or GitHub issue |

### 3.1 User Requirements

Fundamental constraints from the project owner:

| # | Requirement | Detail |
|---|-------------|--------|
| UR-01 | **Alarm works without network while powered** | After synchronization, keep evaluating saved alarms during HA/Wi-Fi outages. After reboot, wait for valid time; apply the five-minute catch-up window. |
| UR-02 | **Automatic RTC synchronization via NTP** | ESP32 syncs RTC via NTP when network is available. No manual time setting. |
| UR-03 | **Local alarm storage** | Alarm times and settings stored in non-volatile memory (NVS/Preferences) on the ESP32. Alarms survive reboots and network outages. |
| **UR-04** | **Reminders (deferred)** | Reminder text, speech, and offline fallback need a later design. No spoken-reminder implementation or offline speech guarantee is required for v1. |
| **UR-05** | **Alarms always fire — no DND suppression** | Alarm clocks are never suppressed by "Do Not Disturb" or "Sleep Mode". This is a universal UX rule (Alexa, physical alarms). Reminders may optionally be muted by DND. |
| **UR-06** | **Multiple devices — only the set device rings** | When multiple voice PE / Satellite devices exist, only the device where the alarm was set rings. Cross-device sync is out of scope for v1. |

### 3.2 Community-Driven Requirements

| # | Requirement | Detail | Source |
|---|-------------|--------|--------|
| R-voice-01 | Set alarms via voice commands through the Voice Assistant pipeline | Last barrier preventing HA Voice PE users from replacing Nest/Alexa | [C #853987], [C #559] |
| R-voice-02 | Verbal confirmation after setting an alarm | "Okay, your alarm is set for 7 AM" — builds user confidence | [C #853987] |
| R-voice-03 | Stop ringing by voice: "stop" | Must work like Voice PE timers — parity expected | [C #559] |
| R-voice-04 | Support alarms and reminders via intents | Same intent framework as existing timers | [C #862776] |
| R-offline-01 | Alarm fires independently on ESP32 | Not dependent on HA being online | [C #559], [C #467] |
| R-offline-02 | Alarm persistent without HA once set | Similar to timer behavior | [C #467] |
| R-offline-03 | RTC with automatic NTP synchronization | **Mandatory** for offline operation | **UR-02** |
| R-offline-04 | Local alarm storage in NVS/Preferences | **Mandatory** for reboot/network resilience | **UR-03** |
| R-offline-05 | Physical button to stop/snooze alarm | Hardware fallback when voice is unavailable | [C #559], [C #467] |
| R-offline-06 | Default alarm sound plays locally | No network required for alarm audio | [C #559] |
| R-ha-01 | Set alarms from HA automations | Programmatic alarm triggering | [C #823282] |
| R-ha-02 | Alarm entity via ESPHome native API | Other satellites/devices can interact with alarm state | [C #559] |
| R-ha-03 | Seamless integration with HA alarm-related integrations | Compatible with existing/future integrations | [C #1089] |
| R-ha-04 | Alarm state visible in HA frontend / dashboards | Lovelace cards for alarm management | [C #910037] |
| R-ha-05 | Current alarm state propagated to HA | idle / ringing / snoozing → HA automation triggers | [C #1089] |
| R-audio-01 | Stop alarms and reminders by device button | Physical button as primary dismissal method | [C #862776] |
| R-audio-02 | Allow custom ringtones via media sources or URL | Not limited to built-in tones | [C #862776] |
| R-audio-03 | Speaker audio detection | Verify alarm is actually audible | SOAS |
| R-audio-04 | Alarm volume independent of media volume | Separate volume control for alarm sounds | [C #559], Alexa 2.4 |

### 3.3 Functional Requirements

| # | Feature | Priority | Detail | Sources |
|---|---------|----------|--------|---------|
| **FR-01** | Single alarm set/delete | P0 | Set and delete one-time alarms on the ESP32 | [A 2.1.1], [UR-01] |
| **FR-02** | Repeating alarm | P0 | Daily / weekday / custom day-of-week patterns | [A 2.1.2], [UR-01] |
| **FR-03** | RTC-backed alarm firing | P0 | Alarm fires independently on ESP32 without network | [UR-01], [NFR-01] |
| **FR-04** | RTC NTP synchronization | P0 | Automatic sync when network is available | [UR-02], [NFR-08] |
| **FR-05** | Local alarm storage | P0 | Alarms stored in NVS on ESP32 flash | [UR-03], [NFR-07] |
| **FR-07** | Multiple alarms | P0 | Default eight entries. Due alarms share a ringing session; one dismissal consumes all already-due occurrences and preserves future ones. | [A 2.1.8] |
| **FR-35** | Snooze | P3 | Configurable duration (default 9 min). Creates a new NVS entry. Fires `alarm_snoozed` event for HA integration. (v3 feature, detailed spec TBD) | [A 2.2], [R-offline-05] |
| **FR-08** | Default alarm sound | P0 | Local timer tone via the existing audio pipeline. Per-alarm sound selection is deferred. | [A 2.3.1], [R-audio-02] |
| **FR-09** | Alarm volume control | P0 | Independent of media volume. Ascending volume (fade-in) is v2. | [A 2.4.1], [R-audio-04] |
| **FR-10** | Physical stop button | P0 | Existing center button dismisses the ringing session offline. Snooze is deferred. | [R-offline-05], [R-audio-01], [A 2.5.1] |
| **FR-11** | Alarm state entity via ESPHome API | P0 | State exposed to Home Assistant | [R-ha-02], [R-ha-05] |
| **FR-12** | Voice commands | P1 | Set/cancel in German and English through HA Assist; bare hours use 24-hour time and the next occurrence. | [R-voice-01] |
| **FR-13** | Voice dismissal | P1 | Local wake word immediately dismisses ringing, including offline; no subsequent command required. | [R-voice-03], [A 2.5.1] |
| **FR-14** | Verbal confirmation | P1 | Confirm resolved schedule only after the device acknowledges durable storage, in the Assist pipeline language. | [R-voice-02] |
| **FR-15** | Alarm listing | P1 | "What alarms are set?" | [A 2.1.5] |
| **FR-16** | Default alarm sound (offline) | P1 | Built-in tones play without network | [R-offline-06] |
| **FR-17** | Alarm volume software abstraction | P1 | Standardized entity interface, device-agnostic | [A 2.4.1], [R-audio-04] |
| **FR-18** | Single reminder set/delete | P1 | One-time reminder on ESP32 | [UR-04], [A 2.10.1] |
| **FR-19** | Reminder notification text | P1 | TTS speaks user-defined text on fire | [UR-04], [A 2.10.3] |
| **FR-20** | Reminder listing | P2 | "What reminders do I have?" | [A 2.10.4] |
| **FR-21** | Reminder dismissal | P2 | Stop a ringing reminder | [A 2.10.5] |
| **FR-22** | Reminder deletion | P2 | Cancel a scheduled reminder | [A 2.10.6] |
| **FR-23** | Music alarm | P2 | Play media via HA media player | [A 2.3.2] |
| **FR-24** | Named alarms | P2 | Labels for alarm management | [A 2.1.3] |
| **FR-25** | Duration-based reminder | P2 | "remind me in 20 minutes" | [A 2.10.2] |
| **FR-26** | HA alarm management | P1 | `voice_alarm.set_alarm`, `delete_alarm`, `stop`, and programmatic `list_alarms`; one explicit device target outside voice context. | [R-ha-01] |
| **FR-27** | Alarm-triggered HA automations | P0 | Live per-occurrence `esphome.voice_alarm_fired` event; no replay of offline events on reconnect. | [A 2.7.1], [R-ha-04] |
| **FR-28** | Reminders via intents | P2 | Full intent framework support | [R-voice-04] |
| **FR-29** | Custom ringtones (URL/media source) | P3 | Not limited to built-in tones | [R-audio-02] |
| **FR-30** | Alarm state in Lovelace dashboard | P3 | Visual alarm management | [R-ha-04] |
| **FR-31** | Per-alarm sound selection | P2 | Different sound per alarm (e.g. sirene Mo-Fr, radio Sa/So) | [A 2.3.1] |
| **FR-32** | Ascending volume (fade-in) | P2 | Volume starts at 20%, rises to 100% over 5 min | [A 2.4.2] |
| **FR-33** | Information briefing | P4 | Weather, calendar at wake time | [A 2.7.4] |
| **FR-34** | Audio detection | P4 | Verify speaker output is audible | [R-audio-03] |

### 3.4 Out of Scope

These boundaries distinguish excluded work from features deferred beyond v1.

**Smart Home Routines (firmware)** — HA automations use the alarm state or live `esphome.voice_alarm_fired` event [FR-27]. The firmware does not define routines or replay missed automation events.

**Deferred from v1** — Reminders, snooze, Satellite1, dedicated LLM/MCP support, music/custom tones, fade-in, named alarms, voice listing, and custom dashboards. The stock device does not need to recover time without network after reboot.

**Physical Hardware** — Clock displays, night mode, speaker hardware, button design are out of scope. The project is firmware-only and runs on existing devices. Software abstractions for alarm volume and button control are in scope.

**Timers** — Count-down timers are already implemented in Home Assistant Voice PE and are not part of this project. The Timer implementation serves as our **reference architecture** for state machine design, NLU intent processing, and audio output — see §2.3.

**Display support** — Display output is out of scope for v1. If the community requests display support for alarm management, a text-mode display interface can be added in a future version.

**Cross-device alarm sync** — When multiple voice PE / Satellite devices exist, alarms fire only on the device where they were set. Cross-device alarm propagation via HA is out of scope for v1.

### 3.5 Design Decisions

The owner interview and [ADR-009](arch/ADR-009-v1-implementation-baseline.md) replace the previous D1–D19 design baseline. Product choices are accepted; technical defaults are explicitly distinguished in that ADR.

| Area | Current v1 decision |
|------|---------------------|
| Hardware/audio | Voice PE first; reuse upstream audio, button, and local wake-word facilities. |
| Scope | Alarms only. Reminders, snooze, other hardware, and dedicated LLM/MCP support are deferred. |
| Offline/reboot | Continue while powered with valid time; reboot waits for synchronization. Catch up occurrences at most 300 seconds late. |
| Time interpretation | Bare hours use 24-hour time and the next occurrence; explicit dates/qualifiers override. Align HA/device timezone. |
| DST | Preserve repeating local time; skip spring gaps and fire once across autumn repeated times. |
| Ringing | One shared session for already-due alarms; local wake word/button dismiss immediately. Stop automatically after 15 minutes. |
| Scheduling | One-second deadline checks; continue evaluating while ringing, track handled occurrences, and retain future repeating occurrences. |
| Persistence | Versioned serialization with stable IDs and occurrence bookkeeping; default eight alarms. Durable save before success acknowledgment. |
| Weekdays | Monday-first mask everywhere: weekdays = 31, daily = 127, one-shot = 0 plus explicit date. |
| HA integration | ESP32 is authoritative. The custom integration implements targeted `voice_alarm` services/intents using the native API. |
| HA state/events | Firmware-backed state/firing/volume; live `esphome.voice_alarm_fired` events; full snapshot refresh on reconnect without stale-event replay. |
| Volume | Range 0.1–1.0, default 0.7, step 0.01; persisted separately and independent of media. |
| Voice | German and English through HA Assist; creation/cancellation confirmations follow durable acknowledgment. Dismissal stays local. |

---

## 4. Non-Functional Requirements

| # | Requirement | Detail |
|---|-------------|--------|
| **NFR-01** | **Reliability** | Scheduler detects a due occurrence within one second of the valid device clock; measure real clock accuracy separately. Offline operation follows UR-01 and ADR-009. |
| **NFR-02** | **Latency** | Alarm sound starts within 2 seconds of trigger time. |
| **NFR-03** | **Reboot/power recovery** | Schedules survive power loss. After restart, wait for time synchronization and apply the inclusive five-minute catch-up window. Battery-backed RTC and ringing without device power are outside v1. |
| **NFR-04** | **Privacy** | No voice data sent to external cloud. All NLU runs locally via Home Assistant Voice Assistant. |
| **NFR-05** | **Extensibility** | Keep scheduling independent of device adapters and version stored data. Custom sound delivery is a future feature. |
| **NFR-06** | **Audibility** | Original target: ≥70 dB at 1 meter. Measure on Voice PE before claiming compliance; report a failed target for a requirement/hardware decision. |
| **NFR-07** | **Persistence** | Alarm configuration survives reboots and network outages (NVS/Preferences) [UR-03]. |
| **NFR-08** | **Time accuracy and DST** | NTP synchronizes the clock when available. The original ≤1 second drift/24h target requires measurement on stock hardware; it is not yet verified. Keep recurring local time, skip spring gaps, and ring once in repeated autumn hours. |
| **NFR-09** | **HA/ESPHome standards compliance** | Use standard native API actions/entities and an explicit `voice_alarm` HA integration for services/intents. Pin and validate compatible versions. |

---

## 5. Version Roadmap

| Version | Features |
|---------|----------|
| **v1 (Must Have)** | Voice PE; FR-01–05, FR-07–14, FR-16–17, FR-26–27. One-time/repeating alarms, independent volume, local sound/dismissal, German/English voice setting/cancellation/confirmation, HA services/programmatic listing/events, persistence and agreed offline behavior. |
| **After v1 (prioritize separately)** | FR-15 (voice listing), FR-18–25 (reminders, music, names), FR-28–35 (extended voice/reminders, custom tones/dashboard, fade-in, briefing, audio detection, snooze); Satellite1 and dedicated LLM/MCP support. |

---

## 6. References

### Home Assistant Community Discussions

- [C #559] GH `home-assistant/discussions#559` — Add ability to set alarms on Voice Assistants (soonerfan237, Aug 2025)
- [C #1089] GH `home-assistant/architecture#1089` — Alarm clock support (architectural proposal)
- [C #1046] GH `home-assistant/architecture#1046` — Add basic set of intents for time/tasks management (Feb 2024)
- [C #853987] HA Community #853987 — Alarm clock with voice (Feature Requests, Feb 2025)
- [C #862776] HA Community #862776 — Alarms and Reminders with Intents (Mar 2025)
- [C #823282] HA Community #823282 — Set Alarm on Assistant from within automation (Jan 2025)
- [C #859334] HA Community #859334 — Create alarm on voice assistant from HA (Mar 2025)
- [C #981426] HA Community #981426 — Any word on getting ability to set alarms & reminders? (Jan 2026)
- [C #821612] HA Community #821612 — Automating Timers on HA Voice PE for Wake-up Alerts (Jan 2025)
- [C #886249] HA Community #886249 — DIY ESP32 Alarm Panel with ESPHome (May 2025)
- [C #910037] HA Community #910037 — Alarm clock integration and Lovelace card (Jul 2025)

### ESPHome

- [C #467] GH `esphome/home-assistant-voice-pe#467` — Feature proposal: Local Alarm on the device (Oct 2025)
- [C #1333] GH `esphome/feature-requests#1333` — Support for HA's Input Integrations (Aug 2021)

### Alexa / Alarm Clock References

- [A 2.1–2.10] Amazon Alexa alarm clock feature reference (§2.1)
- [R-alexa] Digital Trends — How to use all Amazon Alexa alarm clock features
- [R-offline] Howtogeek — Amazon Echo Alarms Still Work Without Internet
- [R-volume] Amazon — Change the Volume of Your Alexa Alarms
