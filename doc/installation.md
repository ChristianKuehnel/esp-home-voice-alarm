# Installation Guide

This project ships in two parts, and **both are required**:

1. **ESPHome external component** (`components/voice_alarm`) — the C++ alarm
   scheduler that runs on the ESP32.
2. **Home Assistant custom integration** (`custom_components/voice_alarm`) —
   binds the ESPHome device in Home Assistant and carries the alarm
   set/cancel protocol.

> **Status:** early development (v0.1.0). The device stores schedules in
> flash and works offline once time is synchronized
> (see [ADR-009](arch/ADR-009-v1-implementation-baseline.md)). On voice
> devices (Home Assistant Voice PE, Satellite1), the M3 wiring in
> [Part 1 §3](#3-alarm-audio-volume-dismissal-and-entities-voice-devices)
> additionally rings alarms through the device speaker, applies an
> independent persisted alarm volume, and supports dismissal via the
> device button or the "stop" wake word. These M3 behaviors are implemented
> but not yet hardware-verified (the M3 gates in the [implementation
> plan](implementation-plan.md)). Voice intents for setting and canceling
> alarms are still in progress.

## Prerequisites

- An **ESP32 device** running ESPHome. The primary target is the
  [Home Assistant Voice PE](https://github.com/esphome/home-assistant-voice-pe)
  (its speaker, button, and wake-word hardware drive the alarm). Any
  ESP32 works for the silent scheduler; audible alarms additionally
  require a device package with the hooks listed in
  [Part 1 §3](#3-alarm-audio-volume-dismissal-and-entities-voice-devices)
  (verified: Voice PE, [Satellite1](https://github.com/FutureProofHomes/Satellite1-ESPHome)).
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
- Complete, compile-verified recipes live at
  [`test/voice-pe.yaml`](../test/voice-pe.yaml) (Home Assistant Voice PE)
  and [`test/satellite1.yaml`](../test/satellite1.yaml) (Satellite1). Use
  them as references when adapting your own device config.

### 3. Alarm audio, volume, dismissal, and entities (voice devices)

The configuration so far gives you an offline *scheduler*: alarms arm, fire,
and survive reboots — but nothing audible happens, and Home Assistant shows
no alarm state. On a voice device, add **one line** to your device config:

```yaml
packages:
  voice_alarm.m3: "github://ChristianKuehnel/esp-home-voice-alarm/packages/alarm-wiring.yaml@main"
```

ESPHome fetches the file at compile time and merges it into your device
config — the same mechanism as your device's own voice package. The file
([`packages/alarm-wiring.yaml`](../packages/alarm-wiring.yaml)) adds:

- ringing: a local tone on the device speaker when an alarm fires (repeats
  until dismissed; stops itself after 15 minutes) — no network or Home
  Assistant required,
- an independent persisted **Alarm Volume**, applied for the duration of
  the ring, restoring the previous media volume afterwards,
- dismissal of the ring via the device's own button and the "stop" wake
  word,
- the `Alarm State`, `Alarm Ringing`, `Alarm Snapshot`, and `Alarm Volume`
  entities, the `set_alarm` action (the `esphome.<node_name>_set_alarm`
  service), and the no-reboot Wi-Fi/API policy (ADR-009).

> **Required package hooks.** The wiring reuses two public hooks of the
> device's voice package: the internal `timer_ringing` switch and the
> `external_media_player` media player (no package edits). Verified with the
> official [Home Assistant Voice PE package](https://github.com/esphome/home-assistant-voice-pe)
> (pinned in [`doc/versions.md`](versions.md)) and the
> [FutureProofHomes Satellite1 package](https://github.com/FutureProofHomes/Satellite1-ESPHome)
> (v0.2.1). If your device's package does not provide both, skip this
> section — the scheduler keeps working, silently.
>
> **Id assumptions.** The file expects `id: alarm_clock` on your
> `voice_alarm:` block (§2) and the two hooks above. If you use different
> ids, copy the file into your own configuration and adjust the references.
>
> **Already added the blocks manually?** An earlier version of this guide
> listed the M3 YAML inline. Remove those blocks (`api.actions`,
> `voice_alarm.on_state`, the `alarm_*` globals/interval/entities, and the
> `reboot_timeout` lines) before adding the include — lists merge by
> concatenation, so duplicates would fail the compile.

Complete, compile-verified recipes that include this same file:
[`test/voice-pe.yaml`](../test/voice-pe.yaml) (Voice PE) and
[`test/satellite1.yaml`](../test/satellite1.yaml) (Satellite1). CI compiles
both on every push, so the wiring stays verified.

**Device-specific behavior:**

| | Home Assistant Voice PE | Satellite1 |
|---|---|---|
| Dismissal button | center button, single click | action button (right), single click |
| Audible alarm volume | bounded to ≈0.4–0.85 by the hardware remap | 0.1–1.0 |
| Mute caveat | — | the left button is a master mute: while latched the ring is inaudible but can still be dismissed |

**Where it shows up in Home Assistant:**

- The four `Alarm *` entities appear **on the ESPHome device** (Settings →
  Devices & Services → your device → Entities). The Voice Alarm Clock
  integration itself creates no entities — it only binds the device.
- Setting an alarm is the `esphome.<node_name>_set_alarm` service (node name
  with `-` replaced by `_`, e.g. `esphome.my_voice_pe_set_alarm`). Data:
  `request_id` (any unique int), `expected_revision` (read from the
  `Alarm Snapshot` entity), `hour`, `minute`, `days_mask`
  (`0` = one-shot, `31` = weekdays, `127` = daily), and the date fields
  `year`, `month`, `day` (required for one-shots: a valid calendar date in
  1971–2100 that has not passed and is not in a DST gap; ignored for
  repeating alarms). A scheduled one-shot is visible in the
  `Alarm Snapshot` entity as `…;one_shot=YYYY-MM-DD HH:MM`. Until the voice
  intents land (implementation plan M4/M5), this service is the way to set
  alarms.

### 4. Compile and flash

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
| No `Alarm *` entities in Home Assistant after flashing | The M3 wiring include ([Part 1 §3](#3-alarm-audio-volume-dismissal-and-entities-voice-devices)) is missing from the device config — the integration itself creates no entities. Add the one-line include, recompile, and flash. |
| `esphome.<device>_set_alarm` service not found | The M3 wiring include (Part 1 §3) is missing from the device config. Add the one-line include, recompile, and flash. |
| Duplicate entity/action names after adding the include | You merged the M3 blocks manually *and* included the wiring file. Remove the manual blocks (they are now in the file), recompile, and flash. |
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
