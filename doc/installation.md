# Installation Guide

This project ships in two parts, and **both are required**:

1. **ESPHome external component** (`components/voice_alarm`) — the C++ alarm
   scheduler that runs on the ESP32.
2. **Home Assistant custom integration** (`custom_components/voice_alarm`) —
   binds the ESPHome device in Home Assistant and carries the alarm
   set/cancel protocol.

> **Status:** early development (v0.1.0). Today the device stores schedules in
> flash, rings alarms through the device speaker, works offline once time is
> synchronized, and supports dismissal via the physical button or wake word
> (see [ADR-009](arch/ADR-009-v1-implementation-baseline.md)). Voice intents
> for setting and canceling alarms are still in progress
> ([implementation plan](implementation-plan.md)).

## Prerequisites

- An **ESP32 device** running ESPHome. The primary target is the
  [Home Assistant Voice PE](https://github.com/esphome/home-assistant-voice-pe)
  (its speaker, button, and wake-word hardware drive the alarm).
- **Home Assistant** (Core, Container, or OS) with the built-in
  **ESPHome** integration. The device must already be paired with HA so it
  appears in the device registry.
- Home Assistant **2026.2.3 or newer**.
- For compiling/flashing firmware: the ESPHome add-on, or the ESPHome CLI.
  The verified toolchain baseline is [doc/versions.md](versions.md).

---

## Part 1 — ESPHome configuration

### 1. Add the external component

Add this to your device's YAML. ESPHome clones the repository at build time
and picks up `components/voice_alarm` from the repo root automatically:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ChristianKuehnel/esp-home-voice-alarm
      ref: main  # or a release tag such as v0.1.0 for reproducible builds
    components:
      - voice_alarm
```

### 2. Add a time source and the `voice_alarm` block

The scheduler needs a `time` platform to know the wall clock. Add one if your
config does not already have one, and reference it by id:

```yaml
time:
  - platform: sntp
    id: alarm_time
    timezone: Europe/Berlin  # your IANA time zone

voice_alarm:
  id: alarm_clock
  time_id: alarm_time
```

Notes:

- If your base config already defines a `time` platform (some device recipes
  do), just give it an `id:` and reuse that id in `time_id:`.
- Until the first time synchronization succeeds after a reboot, schedules stay
  armed but cannot fire. Occurrences missed by at most five minutes ring on
  sync; older ones are skipped.
- A complete, compile-verified Voice PE recipe — including the pinned
  upstream package, Wi-Fi/API no-reboot settings, and the `voice_alarm`
  binding — lives at [`test/voice-pe.yaml`](../test/voice-pe.yaml). Use it as
  the reference when adapting your own Voice PE config.

### 3. Compile and flash

- **CLI:** `esphome run your-device.yaml` (or `esphome compile` to validate
  only).
- **ESPHome add-on:** open the device in the dashboard and use
  *Compile & Flash*, or *Install to device* (OTA) if the device is already
  reachable.

---

## Part 2 — Home Assistant

### Option A: Home Assistant Core (manual install)

1. Get the source, e.g.:

   ```sh
   git clone https://github.com/ChristianKuehnel/esp-home-voice-alarm
   ```

2. Copy the integration folder into your HA configuration directory, keeping
   the folder name `voice_alarm`:

   ```sh
   cp -r esp-home-voice-alarm/custom_components/voice_alarm \
     /etc/homeassistant/custom_components/voice_alarm
   ```

   (Adjust the destination to your config directory, e.g.
   `~/.homeassistant/custom_components/voice_alarm`.)

3. **Restart Home Assistant** (Settings → System → Restart, or
   `systemctl restart homeassistant`).

4. Verify the install: Settings → Devices & Services → *Add Integration* and
   search for **Voice Alarm Clock**.

> The folder **must** live under `custom_components/` and be named exactly
> `voice_alarm` (it is the integration domain).

### Option B: HACS (also works on HA Core)

1. In HACS: ⋯ → **Custom repositories** → add
   `https://github.com/ChristianKuehnel/esp-home-voice-alarm` with category
   **Integration**.
2. Search for **Voice Alarm Clock** and install it. HACS downloads the
   `voice_alarm.zip` release asset (built by CI from
   `custom_components/voice_alarm/`) and places it in `custom_components/`.
3. **Restart Home Assistant.**

### Add the integration

Both options converge here:

1. Settings → **Devices & Services** → *Add Integration*.
2. Search for **Voice Alarm Clock** and select it.
3. Pick the **ESPHome device** that runs the alarm from the dropdown and
   submit.

- If the dropdown is empty, pair your device with the **ESPHome** integration
  first. If the device is not in the registry yet when the flow runs, setup
  fails with a clear message and is retried automatically.
- One integration entry per device — adding a second entry for the same
  device is rejected.

---

## Updating

- **Firmware:** change the git `ref` (new tag or `main`) in
  `external_components:` and recompile/reflash the device.
- **Integration:** via HACS, use the update button; manually, re-copy the
  `custom_components/voice_alarm/` folder from a fresh checkout and restart
  HA.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| ESPHome: `Could not find components folder for source` | Check the `url` and `ref` — the cloned repo must contain a `components/` directory at its root. |
| ESPHome config validation error on `voice_alarm: time_id` | `time_id` must reference an existing `time` platform with that id. |
| Integration setup keeps failing: *"The configured ESPHome device is not in the device registry yet"* | The device is not (yet) paired via the ESPHome integration. Pair it; setup retries on its own. |
| Alarm did not fire after a reboot | The device was still unsynchronized; NTP sync is required before firing. Occurrences more than five minutes stale are skipped by design. |
| Nothing changes after "updating" the ESPHome component | The component only changes firmware — you must recompile and flash the device. Restarting HA alone is not enough. |

## References

- [ESPHome External Components](https://esphome.io/components/external_components/)
- [HACS custom repositories](https://hacs.xyz/docs/advanced/custom_repositories)
- [Installation & repository structure ADR](arch/ADR-007-installation-repo-structure.md)
- [Pinned compatibility matrix](versions.md)
