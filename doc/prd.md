# Product Requirements Document — Alarm Clock

> Reference: Amazon Alexa alarm features + Home Assistant Community Feedback
>
> **Last updated:** 2026-08-26

---

## 1. Vision

Build a voice-controlled alarm clock running on **ESPHome** + **Home Assistant** that delivers the same core alarm experience as **Amazon Alexa** — without requiring a cloud-based voice assistant or proprietary hardware.

The alarm runs **independently on the ESP32 device**. Once set, it fires at the correct time even if Home Assistant reboots, goes offline, or the Wi-Fi network is down. The ESP32 maintains its own real-time clock (RTC) with automatic NTP synchronization and stores alarm and reminder data locally in non-volatile memory (NVS).

The firmware runs on ESP32-S3-based voice assistant hardware, specifically:

| Device | URL | Key Specs |
|--------|-----|-----------|
| **Home Assistant Voice PE** | [home-assistant.io/voice-pe](https://www.home-assistant.io/voice-pe/) | ESP32-S3, XMOS XU316, 4-mic array, LCD, button, buzzer |
| **FutureProofHomes Satellite1** | [futureproofhomes.net](https://futureproofhomes.net/products/satellite1-smart-speaker) | ESP32-S3, XMOS XU316, 4-mic array, 25W amp, headphone, mmWave, sensors |

Both devices share ESP32-S3 SoC + XMOS I2S audio pipeline + ESPHome-native firmware. Device-specific ESPHome config files (one per device) bind hardware components to the common firmware abstractions.

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

**Snooze** — Pause the alarm for a configurable interval (default ~9 min) via voice or button.

**Alarm Sounds** — Built-in tones, music alarm (HA media player), sound selection per alarm.

**Volume & Wake Behavior** — Independent alarm volume, ascending volume (fade-in), silence-after timer.

**Alarm Dismissal** — Stop (silently dismiss), snooze, cancel (delete) via voice or button.

**Offline Behavior** — Alarm fires without Wi-Fi using local RTC; default tone fallback for cloud-dependent sounds; offline stop works.

**Smart Home Routines** — Alarm-triggered automations (lights, thermostat, news).

**Physical Hardware** — Clock display, night mode, speaker hardware, button layout.

**Timers** — Count-down timers with listing, naming, and stop.

### 2.2 Technical Foundations — What Exists in ESPHome & HA

This project builds on extensively battle-tested ESPHome and Home Assistant infrastructure. The table below shows what we **reuse** versus what we **implement**:

| Layer | Component | Status | Usage in our project |
|-------|-----------|--------|---------------------|
| **Voice Pipeline** | ESPHome `voice_assistant` / `assist_pipeline` | ✅ Existing | Wake Word → Transcription → Assist Intents → Audio Response. We extend intents with `SetAlarm`, `CancelAlarm`, `SetReminder`. |
| **Timer State Machine** | Timer implementation in ESPHome | ✅ Existing | State machine (idle → counting → ringing → stopped), NLU intent parsing, NTP sync, audio output (Fogbeep, TTS, fade-in). Our reference model. |
| **ESPHome Native API** | aioesphomeapi | ✅ Existing | Standard entity communication (BinarySensor, Number, Text, etc.) between HA and ESP32. No custom protocols. |
| **NVS / Preferences** | ESPHome NVS storage | ✅ Existing | Persistent storage on ESP32 flash for alarms and reminders across reboots. |
| **NTP Client** | ESPHome NTP component | ✅ Existing | RTC synchronization for long-term accuracy. |
| **I2S Audio** | ESPHome `i2s_audio` + XMOS pipeline | ✅ Existing | Audio output to speaker. Voice PE already uses XMOS XU316 I2S pipeline. |
| **Binary Sensor** | ESPHome `binary_sensor` | ✅ Existing | Physical button detection (`ONCE`/`DOUBLE_CLICK`/`LONG_PRESS`). |
| **Media Player** | ESPHome `media_player` component | ✅ Existing | Volume control and music alarm playback (v2). |
| **ESPHome Config Schema** | Native YAML config system | ✅ Existing | Device-specific config files for Voice PE and Satellite1. |
| **HA Assist Intents** | `SetTimer`, `CancelTimer`, `GetTime` | ✅ Existing | We extend the intent system with alarm/reminder intents in the same pattern. |

**What we implement (not existing):**

| Component | What it does |
|-----------|-------------|
| **Alarm State Machine** | Offline-first: alarm ticks independently on ESP32 RTC, not HA-driven. |
| **NVS Alarm Storage** | Structured storage for alarm/reminder entries (times, recurrence, sound, text). |
| **Local RTC Timer Tick** | Periodic check (e.g. every second) against local RTC, not HA polling. |
| **Reminder Text + TTS** | User-defined notification text stored locally, spoken via TTS on fire. |
| **Custom ESPHome Component** | Unified alarm + reminder entity exposing the state machine via ESPHome API. |

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
| UR-01 | **RTC-backed alarm — works without network** | The ESPHome device maintains its own RTC. Alarms fire at the correct time regardless of network connectivity. |
| UR-02 | **Automatic RTC synchronization via NTP** | ESP32 syncs RTC via NTP when network is available. No manual time setting. |
| UR-03 | **Local alarm storage** | Alarm times and settings stored in non-volatile memory (NVS/Preferences) on the ESP32. Alarms survive reboots and network outages. |
| UR-04 | **Reminders (time-based notifications)** | Users set a one-time reminder with configurable notification text (e.g. "drive now"). Device speaks the message aloud when the reminder fires. Works offline like alarms. |

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
| **FR-06** | Snooze | P0 | Configurable duration (default 9 min) | [A 2.2], [R-offline-05] |
| **FR-07** | Multiple simultaneous alarms | P0 | Independent alarms, no explicit limit | [A 2.1.8] |
| **FR-08** | Alarm sound selection | P0 | At least 3 built-in tones | [A 2.3.1], [R-audio-02] |
| **FR-09** | Alarm volume control | P0 | Independent of media volume | [A 2.4.1], [R-audio-04] |
| **FR-10** | Physical snooze/stop button | P0 | Button detection and state-machine response | [R-offline-05], [R-audio-01], [A 2.5.1] |
| **FR-11** | Alarm state entity via ESPHome API | P0 | State exposed to Home Assistant | [R-ha-02], [R-ha-05] |
| **FR-12** | Voice command: set alarms | P1 | Through Voice Assistant pipeline | [R-voice-01] |
| **FR-13** | Voice command: stop ringing | P1 | "stop" response while ringing | [R-voice-03], [A 2.5.1] |
| **FR-14** | Verbal confirmation | P1 | Confirms alarm was set | [R-voice-02] |
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
| **FR-26** | Set alarm from HA automations | P3 | Service call trigger | [R-ha-01] |
| **FR-27** | Alarm-triggered HA automations | P3 | Alarm dismissed → trigger routines | [A 2.7.1], [R-ha-04] |
| **FR-28** | Reminders via intents | P3 | Full intent framework support | [R-voice-04] |
| **FR-29** | Custom ringtones (URL/media source) | P3 | Not limited to built-in tones | [R-audio-02] |
| **FR-30** | Alarm state in Lovelace dashboard | P3 | Visual alarm management | [R-ha-04] |
| **FR-31** | Pre-alarm routines (gradual light-up) | P4 | Fade-in behavior before alarm fires | [A 2.4.2] |
| **FR-32** | Information briefing | P4 | Weather, calendar at wake time | [A 2.7.4] |
| **FR-33** | Audio detection | P4 | Verify speaker output is audible | [R-audio-03] |

### 3.4 Out of Scope

These features are explicitly excluded from this project.

**Smart Home Routines** — Alarm-triggered automations (lights, thermostat, news briefings) are out of scope [A 2.7]. Morning routines can be implemented as Home Assistant automations triggered by the alarm entity state. This keeps the firmware focused on core alarm functionality.

**Physical Hardware** — Clock displays, night mode, speaker hardware, button design are out of scope. The project is firmware-only and runs on existing devices. Software abstractions for alarm volume and button control are in scope.

**Timers** — Count-down timers are already implemented in Home Assistant Voice PE and are not part of this project. The Timer implementation serves as our **reference architecture** for state machine design, NLU intent processing, and audio output — see §2.3.

---

## 4. Non-Functional Requirements

| # | Requirement | Detail |
|---|-------------|--------|
| **NFR-01** | **Reliability** | Alarm fires at the correct time ±1 second. Offline operation is mandatory [UR-01]. |
| **NFR-02** | **Latency** | Alarm sound starts within 2 seconds of trigger time. |
| **NFR-03** | **Power failure** | RTC maintains time during power loss. ESP32 internal RTC drift ~±20 ppm → needs NTP sync (UR-02). External RTC (DS3231, ±2 ppm) for production hardware. |
| **NFR-04** | **Privacy** | No voice data sent to external cloud. All NLU runs locally via Home Assistant Voice Assistant. |
| **NFR-05** | **Extensibility** | Architecture supports adding new alarm sounds and trigger actions without firmware updates. |
| **NFR-06** | **Audibility** | Alarm sound ≥70 dB at 1 meter with typical speaker hardware. |
| **NFR-07** | **Persistence** | Alarm configuration survives reboots and network outages (NVS/Preferences) [UR-03]. |
| **NFR-08** | **NTP accuracy** | RTC drift compensated by NTP sync when network available. ≤1 second drift after 24h offline. |
| **NFR-09** | **HA/ESPHome standards compliance** | All interfaces follow ESPHome native API (aioesphomeapi), HA entity model (BinarySensor, Number, Text, etc.), service schemas, and config structure. Enables seamless upstream integration into ESPHome and HA without custom adapters. |

---

## 5. Version Roadmap

**v1 (Must Have)** — FR-01 through FR-19: Core alarm + single reminder with voice commands, offline RTC, snooze, multiple alarms, sound selection, volume control, physical button, HA entity exposure.

**v2 (Should Have)** — FR-20 through FR-25: Extended reminders, music alarm via HA media player, named alarms.

**v3+ (Nice to Have)** — FR-26 through FR-33: HA automation triggers, custom ringtones, dashboard cards, pre-alarm routines, information briefings.

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