# Product Requirements Document — Alarm Clock

> Reference: Alexa alarm clock features (Amazon Echo / Echo Dot / Echo Spot) + Home Assistant Community Feedback
>
> **Last updated:** 2026-08-26

---

## 1. Vision

Build a voice-controlled alarm clock running on **ESPHome** + **Home Assistant** that delivers the same core alarm experience as **Amazon Alexa**, without requiring a cloud-based voice assistant or proprietary hardware.

The alarm runs **independently on the ESP32 device**. Once set, it fires at the correct time even if Home Assistant reboots, goes offline, or the Wi-Fi network is down. The ESP32 maintains its own real-time clock (RTC) with automatic NTP synchronization and stores alarm times locally in non-volatile memory.

The firmware must run on standard ESP32-S3-based voice assistant hardware. Specifically, the following target devices are required:

| # | Device | URL | Key Specs |
|---|--------|-----|-----------|
| D-1 | **Home Assistant Voice PE** | [home-assistant.io/voice-pe](https://www.home-assistant.io/voice-pe/) | ESP32-S3, XMOS XU316 audio processor, 4-microphone array, LCD display, button, buzzer |
| D-2 | **FutureProofHomes Satellite1** | [futureproofhomes.net](https://futureproofhomes.net/products/satellite1-smart-speaker) | ESP32-S3, XMOS XU316, 4-mic array, 25W amplifier, headphone jack, LD2450 mmWave sensor, temperature/humidity/luminosity sensors |

Both devices share a common foundation: ESP32-S3 SoC, XMOS voice processor (I2S audio pipeline), and ESPHome-native firmware. This PRD targets a firmware that runs on both platforms, with device-specific configuration files for each.

---

## 2. Alexa Alarm Feature Reference

The following features are observed from Amazon's Alexa alarm implementation across Echo, Echo Dot, and Echo Spot devices. These form the baseline feature set this project targets.

### 2.1 Alarm Creation & Management

| # | Feature | Description | Voice Command Example |
|---|---------|-------------|----------------------|
| 2.1.1 | Single alarm | Set one-time alarm at a specific time | _"Alexa, set an alarm for 7 AM"_ |
| 2.1.2 | Repeating alarm | Set alarm to repeat on selected days | _"Alexa, set an alarm for 7 AM every day"_, _"every weekday"_, _"every Saturday"_ |
| 2.1.3 | Named alarm | Label alarms for easy management (useful when multiple alarms exist) | _"Alexa, set an alarm called Gym for 6 AM on weekdays"_ |
| 2.1.4 | AM/PM disambiguation | If only hour is given, Alexa prompts AM or PM (or defaults to the closest time) | _"Alexa, set an alarm for 7"_ → Alexa asks AM or PM |
| 2.1.5 | Alarm listing | Query all currently set alarms | _"Alexa, what alarms are set?"_ |
| 2.1.6 | Alarm editing | Modify an existing alarm's time, sound, or days | Via Alexa app or _"Alexa, change my 7 AM alarm to 8 AM"_ |
| 2.1.7 | Alarm deletion | Cancel a specific alarm or all alarms | _"Alexa, cancel my 7 AM alarm"_, _"Alexa, delete all alarms"_ |
| 2.1.8 | Multiple alarms | Support multiple independent alarms simultaneously | Default capability, no explicit limit known |

### 2.2 Snooze

| # | Feature | Description | Voice Command Example |
|---|---------|-------------|----------------------|
| 2.2.1 | Snooze on ringing | Pause the alarm for a configurable interval (default ~9 minutes) | _"Alexa, snooze"_ |
| 2.2.2 | Snooze length | Configurable snooze duration (typically 1–30 minutes) | Set via Alexa app — Alarms & Timers → Settings |

### 2.3 Alarm Sounds

| # | Feature | Description | Voice Command Example |
|---|---------|-------------|----------------------|
| 2.3.1 | Default tones | Built-in alarm tones (several pre-loaded sounds) | Default behavior |
| 2.3.2 | Music alarm | Wake up to a specific song, artist, or playlist | _"Alexa, wake me up to Bohemian Rhapsody at 7 AM"_ |
| 2.3.3 | Radio alarm | Wake up to a radio station | _"Alexa, set alarm to BBC Radio 1 at 7 AM"_ |
| 2.3.4 | Service integration | Amazon Music, Spotify, Pandora, and other linked services available as alarm sounds | Set via Alexa app — Alarms & Timers → Sound |
| 2.3.5 | Artist radio | Play an internet radio station based on an artist | _"Alexa, wake me up to Coldplay radio"_ |
| 2.3.6 | Sound selection per alarm | Each alarm can have its own individual sound | Set via Alexa app per alarm |

### 2.4 Volume & Wake Behavior

| # | Feature | Description | Voice Command Example |
|---|---------|-------------|----------------------|
| 2.4.1 | Alarm volume | Set alarm volume independently of media volume | Alexa app — Alarms & Timers → Settings → Volume |
| 2.4.2 | Ascending volume | Gradually increase alarm volume over time for gentler wake-up | Alexa app toggle — also settable via voice: _"set an alarm with ascending volume"_ |
| 2.4.3 | Silence after | Auto-stop alarm after a configurable duration | Alexa app — Alarms & Timers → Settings → Silence after |

### 2.5 Alarm Dismissal

| # | Feature | Description | Voice Command Example |
|---|---------|-------------|----------------------|
| 2.5.1 | Stop alarm | Silently dismiss the alarm | _"Alexa, stop"_ / _"Alexa, turn off"_ |
| 2.5.2 | Snooze | See 2.2.1 | _"Alexa, snooze"_ |
| 2.5.3 | Cancel alarm | Cancel (delete) the ringing alarm entirely | _"Alexa, cancel"_ |

### 2.6 Offline / Fallback Behavior

| # | Feature | Description |
|---|---------|-------------|
| 2.6.1 | Offline alarm firing | The alarm still fires at the correct time even without Wi-Fi / internet. A local RTC keeps time. |
| 2.6.2 | Sound fallback | If Wi-Fi is unavailable and the alarm was configured to play music/radio, Alexa falls back to the default alarm tone after ~1 minute. |
| 2.6.3 | Offline stop | _"Alexa, stop"_ works to silence the alarm offline. |
| 2.6.4 | Offline limitations | Voice commands like _"snooze"_ or _"cancel"_ may not work offline (requires cloud NLU). |

### 2.7 Out of Scope — Smart Home Integration (Morning Routines)

| # | Feature | Description |
|---|---------|-------------|
| 2.7.1 | Alarm-triggered routines | When an alarm is dismissed, trigger automations (routines) — e.g., turn on lights, adjust thermostat, play news. |
| 2.7.2 | Scheduled routines | Independent of alarm: routines triggered by schedule (e.g., lights gradually brighten 30 min before wake-up). |
| 2.7.3 | Device actions | Control smart lights, plugs, thermostats, blinds, TV, and any Alexa-compatible device as routine actions. |
| 2.7.4 | Information delivery | Weather, calendar, news briefings as part of morning routine. |

Morning Routines und Smart Home Automationen sind **Out of Scope** für dieses Projekt. Die Alarm-Funktionalität bleibt auf das reine Wecker-Feature beschränkt (Firing, Snooze, Dismissal, Sound). Smart Home Integration über HA/Automations kann bei Bedarf über Home Assistant-Konfiguration nachgeschaltet werden und gehört nicht in die ESPHome-Firmware.

### 2.8 Physical Hardware Features (Echo / Echo Dot / Echo Spot)

| # | Feature | Description |
|---|---------|-------------|
| 2.8.1 | Clock display | LED or LCD display showing current time (visible at night). Echo Spot has a color touchscreen. |
| 2.8.2 | Night mode | Dim display at night (auto at 10 PM, configurable). |
| 2.8.3 | Speaker | Built-in speaker for alarm tones and music. |
| 2.8.4 | Button controls | Physical button to snooze/stop alarm (varies by device). |

### 2.9 Timers (Related Feature)

| # | Feature | Description | Voice Command Example |
|---|---------|-------------|----------------------|
| 2.9.1 | Count-down timer | Set a timer for a duration (minutes/hours) | _"Alexa, set a timer for 10 minutes"_ |
| 2.9.2 | Timer listing | Query all active timers | _"Alexa, what timers are running?"_ |
| 2.9.3 | Timer naming | Name timers for easier management | _"Alexa, set a pasta timer for 10 minutes"_ |
| 2.9.4 | Timer stop | Cancel a running timer | _"Alexa, stop the pasta timer"_ |

---

## 3. Home Assistant Community Requirements

The following requirements are derived from ongoing community discussions in the Home Assistant ecosystem. This feature is a well-known gap that prevents users from fully replacing cloud-based assistants.

### 3.1 Key Discussions & Sources

| # | Source | Title | Date |
|---|--------|-------|------|
| 3.1.1 | GH `home-assistant/discussions#559` | Add ability to set alarms on Voice Assistants (soonerfan237) | Aug 7, 2025 |
| 3.1.2 | GH `home-assistant/architecture#1089` | Alarm clock support (architectural proposal) | — |
| 3.1.3 | GH `home-assistant/architecture#1046` | Add basic set of intents for time/tasks management (Alexa parity) | Feb 27, 2024 |
| 3.1.4 | HA Community #853987 | Alarm clock with voice (Feature Requests) | Feb 24, 2025 |
| 3.1.5 | HA Community #862776 | Alarms and Reminders with Intents | Mar 12, 2025 |
| 3.1.6 | HA Community #823282 | Set Alarm on Assistant from within automation | Jan 6, 2025 |
| 3.1.7 | HA Community #859334 | Create alarm on voice assistant from Home Assistant | Mar 6, 2025 |
| 3.1.8 | HA Community #981426 | Any word on getting ability to set alarms & reminders? | Jan 31, 2026 |
| 3.1.9 | HA Community #821612 | Automating Timers on HA Voice PE for Wake-up Alerts | Jan 4, 2025 |
| 3.1.10 | HA Community #847108 | SOAS — Full featured alarm clock with HA integration | Feb 23, 2025 |
| 3.1.11 | GH `esphome/home-assistant-voice-pe#467` | Feature proposal: Local Alarm on the device (Voice PE) | Oct 7, 2025 |
| 3.1.12 | GH `Skons/SOAS` | ESP32 Alarm clock with Home Assistant integration | — |
| 3.1.13 | GH `mmakaay/esphome-alarm-clock` | ESPHome alarm clock example configuration | — |
| 3.1.14 | GH `8bitmcu/ESPHome_AlarmClock` | ESP32/D1 mini based Alarm Clocks running ESPHome | — |
| 3.1.15 | HA Community #886249 | DIY ESP32 Alarm Panel with ESPHome | May 6, 2025 |
| 3.1.16 | HA Community #910037 | Alarm clock integration and Lovelace card | Jul 11, 2025 |
| 3.1.17 | GH `esphome/feature-requests#1333` | Support for HA's Input Integrations (alarm with HA automations) | Aug 2, 2021 |

### 3.2 Community-Driven Requirements

#### 3.2.1 Voice Command Support (from Discussion #559, Community #853987)

| # | Requirement | Rationale |
|---|-------------|-----------|
| R-voice-01 | Set alarms via voice commands through the Voice Assistant pipeline | The last barrier preventing households from replacing Nest/Alexa with HA Voice PE (Community #853987) |
| R-voice-02 | Verbal confirmation after setting an alarm | "Okay, your alarm is set for 7 AM" — builds user confidence |
| R-voice-03 | Stop ringing by voice: "stop" | Must work like Voice PE timers — users expect parity (Discussion #559) |
| R-voice-04 | Support intents for time management: current time query ("What time is it?"), timers, alarms | Architecture #1046 — essential for Alexa/Google Home parity |
| R-voice-05 | Support alarms and reminders via intents | Community #862776 — same intent framework as existing timers |

#### 3.2.2 Local-First / Offline Operation (from Discussion #559, Voice PE #467, User Requirement)

| # | Requirement | Rationale |
|---|-------------|-----------|
| R-offline-01 | Alarm fires independently on the ESP32 — not dependent on HA being online | Discussion #559: "Alarms not affected if Home Assistant reboots or goes temporarily offline overnight" |
| R-offline-02 | Alarm runs locally on the ESP, settable from HA via voice but persistent without HA | Voice PE #467: "Something that is running locally on the ESP... independent from HA once it's set, similar to the timer" |
| R-offline-03 | **RTC with automatic NTP synchronization** — ESP32 has its own real-time clock that syncs when network is available | **User requirement**: Alarm must work with no network connection. RTC maintains time; NTP syncs when online. |
| R-offline-04 | **Local alarm storage** — alarm times stored in non-volatile memory (NVS/Preferences) on the ESP32 | **User requirement**: Alarms persist across reboots and network outages |
| R-offline-05 | Physical button to stop/snooze alarm (hardware fallback when voice is unavailable) | Voice PE #467, Discussion #559 |
| R-offline-06 | Default alarm sound plays locally — no network required for the alarm sound itself | Discussion #559 fallback behavior |

#### 3.2.3 Home Assistant Integration (Community #823282, #859334, Architecture #1089)

| # | Requirement | Rationale |
|---|-------------|-----------|
| R-ha-01 | Ability to set alarms from within HA automations | Community #823282 — users want to trigger alarms programmatically |
| R-ha-02 | Alarm entity exposed via ESPHome native API (aioesphomeapi) so other satellites can interact | Discussion #559: "maybe you also need to extend ESPHome's native API so other satellites can be used" |
| R-ha-03 | Seamless integration with existing/future alarm-related integrations | Architecture #1089: "Enable seamless integration with existing and future alarm-related integrations" |
| R-ha-04 | Alarm state visible in HA frontend / dashboards | Community #910037 — users want Lovelace cards for alarm management |
| R-ha-05 | Current alarm state propagated to HA (idle / ringing / snoozing) | Architecture #1089 — HA needs to know alarm state for automation triggers |

#### 3.2.4 Device & Audio Features (Community #862776, Voice PE #467)

| # | Requirement | Rationale |
|---|-------------|-----------|
| R-audio-01 | Stop alarms and reminders by pressing a button on ESPHome device | Community #862776 — physical button as primary dismissal method |
| R-audio-02 | Allow custom ringtones via media sources or URL inputs | Community #862776 — not limited to built-in tones |
| R-audio-03 | Speaker audio detection — detect if sound is actually coming from the speaker | SOAS / Community feedback — verify alarm is audible |
| R-audio-04 | Volume control for alarm independently of media volume | Discussion #559, Alexa parity |

#### 3.2.5 Current Workarounds & Pain Points (Community #853987, #981426)

| # | Pain Point | Implication |
|---|------------|-------------|
| R-pain-01 | Users create timer automations as a hack for alarms — requires creating/managing many helpers + voice triggers | Community #853987: "For something this critical, I would feel much more confident with a default integration" |
| R-pain-02 | Voice PE users cannot set alarms — missing the one feature that keeps them from fully replacing Google Nest | Community #981426: "I have a nearly perfect voice pipeline setup that does everything I want (except alarms)" |
| R-pain-03 | No way to set alarms on Voice Assistant from automations — forces manual voice command | Community #823282, #859334 |

---

## 4. User Requirements

These are specific requirements from the project owner (Christian Kühnel):

| # | Requirement | Detail |
|---|-------------|--------|
| UR-01 | **RTC-backed alarm — works without network** | The ESPHome device has its own real-time clock (RTC) that keeps time when Wi-Fi is down. Alarms fire at the correct time regardless of network connectivity. |
| UR-02 | **Automatic RTC synchronization via NTP** | When network is available, the ESP32 syncs its RTC via NTP to ensure long-term accuracy. No manual time setting required. |
| UR-03 | **Local alarm storage** | Alarm times are stored locally on the ESP32 in non-volatile memory (NVS/Preferences). Alarms survive reboots and network outages. |

---

## 5. Target Feature Set for v1

Based on the Alexa reference, community feedback, and user requirements, the following features are prioritized for the initial release:

### Must Have (v1)

| Priority | Requirement | Source |
|----------|-------------|--------|
| P0 | Single alarm set/delete (local on ESP32) | Alexa 2.1 + UR-01 |
| P0 | Repeating alarm (daily / weekday / custom days) | Alexa 2.1.2 + UR-01 |
| P0 | **RTC-backed alarm — works without network** | **UR-01** |
| P0 | **Automatic RTC NTP synchronization** | **UR-02** |
| P0 | **Local alarm storage (NVS/Preferences)** | **UR-03** |
| P0 | Snooze (configurable duration, default 9 min) | Alexa 2.2 + R-offline-05 |
| P0 | Multiple simultaneous alarms | Alexa 2.1.8 |
| P0 | Alarm sound selection (at least 3 built-in tones) | Alexa 2.3.1 + R-audio-02 |
| P0 | Alarm volume control | Alexa 2.4.1 + R-audio-04 |
| P0 | **Physical snooze/stop button** | R-offline-05 + R-audio-01 |
| P0 | **Alarm state entity exposed via ESPHome native API** | R-ha-02 + R-ha-05 |
| P1 | Voice command: set alarms | R-voice-01 |
| P1 | Voice command: stop ringing ("stop") | R-voice-03 |
| P1 | Verbal confirmation after setting | R-voice-02 |
| P1 | Alarm listing ("What alarms are set?") | Alexa 2.1.5 + R-voice-04 |
| P1 | **Default alarm sound plays locally (no network)** | R-offline-06 |
| P1 | Clock display on ESP32 | Alexa 2.8.1 |

### Should Have (v2)

| Priority | Requirement | Source |
|----------|-------------|--------|
| P2 | Music alarm (HA media player integration) | Alexa 2.3.2 |
| P2 | Named alarms | Alexa 2.1.3 |
| P2 | Timer support | Alexa 2.9 + R-voice-04 |
| P2 | Night mode (auto-dim display) | Alexa 2.8.2 |

### Nice to Have (v3+)

| Priority | Requirement | Source |
|----------|-------------|--------|
| P3 | Set alarm from HA automations (service call) | R-ha-01 |
| P3 | Alarm-triggered HA automations | Alexa 2.7.1 + R-ha-04 |
| P3 | Reminders via intents | R-voice-05 |
| P3 | Custom ringtones (URL / media source) | R-audio-02 |
| P3 | Alarm state in HA Lovelace dashboard | R-ha-04 |
| P4 | Pre-alarm routines (gradual light-up) | Alexa 2.7.2 |
| P4 | Information briefing (weather, calendar) | Alexa 2.7.4 |
| P4 | Audio detection (verify speaker output) | R-audio-03 |

---

## 6. Non-Functional Requirements

| # | Requirement | Detail |
|---|-------------|--------|
| NFR-1 | **Reliability** | Alarm must fire at the correct time ±1 second. Offline operation is **mandatory** (User Requirement UR-01). |
| NFR-2 | **Latency** | Alarm sound must start within 2 seconds of trigger time. |
| NFR-3 | **Power failure** | RTC must maintain time during power loss. ESP32 internal RTC drift: ~±20 ppm → needs regular NTP sync (UR-02). Consider external RTC module (DS3231, ±2 ppm) for production hardware. |
| NFR-4 | **Privacy** | No voice data sent to external cloud. All NLU runs locally via Home Assistant Voice Assistant. |
| NFR-5 | **Extensibility** | Architecture must support adding new alarm sounds and trigger actions without firmware updates. |
| NFR-6 | **Volume** | Alarm sound must be audible from across a bedroom (≥70 dB at 1 meter with typical speaker). |
| NFR-7 | **Persistence** | Alarm configuration survives reboots and network outages (stored in ESPHome NVS). |
| NFR-8 | **NTP accuracy** | RTC drift compensated by NTP sync when network is available. Target: ≤1 second drift after 24h offline. |
| NFR-9 | **HA/ESPHome standards compliance** | All interfaces — ESPHome native API (aioesphomeapi), Home Assistant entity model (AlarmControlPanel, BinarySensor, Number, Text, etc.), service schemas, and configuration structure — must follow ESPHome and Home Assistant conventions exactly. This ensures the alarm clock can later be integrated seamlessly into the ESPHome and HA codebases without custom adapters or upstream resistance. |

---

## 7. References

### Alexa / Alarm Clock References
- [Digital Trends — How to use all Amazon Alexa alarm clock features](https://www.digitaltrends.com/home/alexa-alarm-clock/)
- [Howtogeek — Amazon Echo Alarms Still Work Without Internet](https://www.howtogeek.com/340198/dont-worry-amazon-echo-alarms-still-work-without-internet/)
- [Smart Home Explained — Alexa Alarm Clock Commands](https://www.smarthomeexplained.com/alexa-alarm-clock-commands-transforming-how-you-wake-up/)
- [Smartenlight — Alexa Alarms, the Smarter Alarm Clock](https://smartenlight.com/alexa-alarm/)
- [Howtogeek / Smarthomeace — How to Set Alarm on Amazon Echo](https://smarthomeace.com/how-to-set-alarm-on-amazon-echo/)
- [Amazon — Change the Volume of Your Alexa Alarms](https://www.amazon.com/gp/help/customer/display.html?nodeId=GJVZBYD6AKDC84ST)

### Home Assistant Community
- [GH `home-assistant/discussions#559` — Add ability to set alarms on Voice Assistants](https://github.com/orgs/home-assistant/discussions/559)
- [GH `home-assistant/architecture#1089` — Alarm clock support](https://github.com/home-assistant/architecture/discussions/1089)
- [GH `home-assistant/architecture#1046` — Add basic set of intents for time/tasks management](https://github.com/home-assistant/architecture/discussions/1046)
- [HA Community #853987 — Alarm clock with voice](https://community.home-assistant.io/t/alarm-clock-with-voice/853987)
- [HA Community #862776 — Alarms and Reminders with Intents](https://community.home-assistant.io/t/alarms-and-reminders-with-intents/862776)
- [HA Community #823282 — Set Alarm on Assistant from within automation](https://community.home-assistant.io/t/set-alarm-on-assistant-from-within-automation/823282)
- [HA Community #859334 — Create alarm on voice assistant from Home Assistant](https://community.home-assistant.io/t/create-alarm-on-voice-assistant-preview-from-home-aassistant/859334)
- [HA Community #981426 — Any word on getting ability to set alarms & reminders?](https://community.home-assistant.io/t/any-word-on-getting-the-ability-to-set-alarms-reminders-using-homeassistant-voice/981426)
- [HA Community #821612 — Automating Timers on HA Voice PE for Wake-up Alerts](https://community.home-assistant.io/t/automating-timers-on-home-assistant-voice-pe-for-wake-up-alerts/821612)
- [HA Community #847108 — SOAS — Full featured alarm clock with HA integration](https://community.home-assistant.io/t/soas-full-featured-alarm-clock-with-home-assistant-integration/847108)
- [HA Community #910037 — Alarm clock integration and Lovelace card](https://community.home-assistant.io/t/alarm-clock-integration-and-lovelace-card/910037)

### ESPHome / Existing Projects
- [GH `esphome/home-assistant-voice-pe#467` — Feature proposal: Local Alarm on the device](https://github.com/esphome/home-assistant-voice-pe/issues/467)
- [GH `Skons/SOAS` — ESP32 Alarm clock with Home Assistant integration](https://github.com/Skons/SOAS)
- [GH `mmakaay/esphome-alarm-clock` — ESPHome alarm clock example](https://github.com/mmakaay/esphome-alarm-clock/blob/main/example.yaml)
- [GH `8bitmcu/ESPHome_AlarmClock` — ESP32/D1 mini based Alarm Clocks](https://github.com/8bitmcu/ESPHome_AlarmClock)
- [GH `esphome/feature-requests#1333` — Support for HA's Input Integrations](https://github.com/esphome/feature-requests/issues/1333)