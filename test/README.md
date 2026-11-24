# Testing without a Voice PE

Run commands from the repository root. The test setup executes the production
C++ scheduler in three environments. It does not emulate the complete Voice PE.

| Layer | Entry point | Coverage today |
|-------|-------------|----------------|
| Native C++ | `python3 test/run_native.py` | The ADR-009 scheduler: model/validation (capacity, stable IDs, one-shot), crossed-deadline firing, 300 s catch-up, clock corrections, DST (spring gap / autumn repeated hour under `America/New_York`), shared ringing session, 15-min monotonic timeout, and versioned storage round trips/corruption — with no ESPHome dependency |
| ESPHome host | `test/host.yaml` + `test/test_host.py` | Compiled ESPHome component, real native API, injected button/wake-word callbacks, disconnect/reconnect, the production `set_alarm` request/ack round trip (accept/stale/invalid), and file-backed preferences: schedule + revision survive restart, a fired occurrence does not refire after reboot, and a corrupt schedule blob is preserved and surfaces an error |
| ESP32-S3 | `test/esp32-s3.yaml` + `test/run_emulator.py` | Actual Xtensa/ESP-IDF binary boot, the same scheduler assertions, ESPHome preferences backed by NVS, and a second boot from saved flash |
 | HA integration | `test/ha/` (pytest suite) | Config flow + entry lifecycle against a pinned `homeassistant` build: registry validation, unload, duplicate prevention, and `set_alarm` request/ack protocol unit tests |
 | Voice PE recipe | `test/voice-pe.yaml` (compile only) | Full official Voice PE hardware recipe: vendored upstream package + `voice_alarm`, NTP, the Wi-Fi/OTA/API reconnect workarounds, and the M3 alarm wiring (shared package `packages/alarm-wiring.yaml`) — compiled, not emulated |
 | Satellite1 recipe | `test/satellite1.yaml` (compile only) | Second verified alarm target: upstream `FutureProofHomes/Satellite1-ESPHome` package at the pinned release + `voice_alarm`, SNTP, and the same M3 wiring package — compiled, not emulated |

The pinned compatibility matrix and clean-environment reproduction commands are
recorded in [`doc/versions.md`](../doc/versions.md).

## Setup

The verified baseline is Python 3.13, ESPHome 2026.9.1, ESP-IDF 5.5.5
(selected by this ESPHome release), and esp-emu 0.49.0 on Linux x86_64.
You need a C++ compiler (`g++`, or set `CXX=clang++`). ESPHome downloads
the other build tools on the first compilation. Allow roughly 2 GB for tools/cache.

```sh
python3.13 -m venv .venv
. .venv/bin/activate
python -m pip install -r test/requirements.txt
export PLATFORMIO_CORE_DIR="$PWD/.tools/platformio"
export XDG_CACHE_HOME="$PWD/.tools/cache"
```

The HA integration suite uses a separate venv (it pins a different
`homeassistant`/`aioesphomeapi` pair than the firmware env):

```sh
python3.13 -m venv .venv-ha
. .venv-ha/bin/activate
python -m pip install -r test/ha/requirements.txt
python -m pytest -q test/ha/
deactivate
```

The installer has checksum-pinned emulator downloads for Linux x86_64/ARM64 and
macOS ARM64. The repository's build/test combination has only been verified on
Linux x86_64; Windows users can use WSL.

## 1. Fast scheduler checks

```sh
python3 test/run_native.py
```

This compiles `components/voice_alarm/alarm_state.cpp` directly with warnings
treated as errors and runs `test/fixtures/scheduler_checks.h` (TZ-independent,
`TZ=UTC0`) and `test/fixtures/dst_checks.h` (under `America/New_York`). Output
is `VOICE_ALARM_NATIVE_PASS`. Build products live in a temporary directory.

The core takes an explicit wall-clock timestamp and monotonic elapsed time;
tests advance time without sleeping. No ESPHome headers or behavior are mocked.
The same `scheduler_checks` assertions also run in the host and firmware
boot smoke tests, so the deterministic core stays in lockstep with the device.

## 2. Host/API integration

```sh
esphome compile test/host.yaml
python -m pytest -q test/test_host.py
```

The test starts and cleans up the compiled process itself. It uses TCP port
16053, refuses to proceed if that port is already in use, and connects using
`aioesphomeapi`. It asserts firing and dismissal through two independently
injected inputs, reconnects to the running process, writes a preferences probe,
then restarts and verifies the restored value. The second test exercises the
production-style `set_alarm` action end to end through
`custom_components/voice_alarm/alarm_protocol.py`: an accepted request advances
the revision and snapshot, a stale request is rejected with the authoritative
revision, an invalid request surfaces the device error message, and a restart
restores both the revision and the schedule. A third test proves a fired
occurrence does not refire after a reboot (durable occurrence bookkeeping), and
a fourth corrupts the stored schedule blob and verifies the device boots,
preserves the blob, and surfaces an error state. A fifth test covers the
M3 alarm volume: the `Alarm Volume` number entity reads and writes the
persisted volume (default 0.7, clamped to 0.1–1.0) and the value survives a
restart; the volume also appears in the `Alarm Snapshot` text.
Preferences and logs are isolated in pytest's temporary directory. Failure
output includes the process log.

### `set_alarm` wire mapping (schema version 2)

The device exposes the user action `set_alarm` with integer variables
`request_id`, `expected_revision`, `hour`, `minute`, `days_mask`, and the
one-shot date fields `year`, `month`, `day`. `days_mask` `0` selects a
one-shot alarm at the explicit date (which must be a valid calendar date in
1971–2100); `1..127` selects a repeating weekday mask (the date fields are
ignored for repeating alarms). HA calls it via
`APIClient.execute_service(service, data, return_response=True)` and receives
an `ExecuteServiceResponse`:

- `success`: `true` only for `status: "ok"`.
- `error_message`: the device-side rejection reason (for example
  `hour must be 0..23`, `the one-shot date is not a valid calendar date`,
  `the one-shot time has already passed`, or
  `expected_revision does not match the device revision`); empty on success.
- `response_data`: JSON, e.g.
  `{"schema_version": 2, "request_id": 9, "status": "ok", "revision": 1}`.
   `revision` is always the device's current authoritative revision;
   `request_id` echoes the request. `status` is one of `ok`, `stale_revision`,
   `invalid_hour`, `invalid_minute`, `invalid_days_mask`, `invalid_date`,
   `past_one_shot`, `nonexistent_time`, `storage_error`.

Rejection semantics: a stale or invalid request changes neither the schedule
nor the revision, and the acknowledgment still reports the current revision so
the caller can refresh and re-issue once. A device whose action is missing,
answers with no payload, times out, or reports another `schema_version`
is considered non-capable: `alarm_protocol` raises `UnsupportedFirmwareError`,
and the caller must refresh device state — never blind-retry the command.
The HA protocol (typed errors `CommandAck`, `SetAlarmRequest`,
`UnsupportedFirmwareError`, `StaleRevisionError`, `VoiceAlarmCommandError`)
lives in `custom_components/voice_alarm/alarm_protocol.py` and depends only on
`aioesphomeapi`, so it is unit-tested in `test/ha/test_alarm_protocol.py` and
reused by this e2e test.

To inspect the fixture manually:

```sh
export ESPHOME_PREFDIR="$PWD/test/.esphome/manual-prefs"
esphome run test/host.yaml
```

HA can connect manually to the development machine's IP and port 16053. This
fixture exposes unauthenticated **test-only** actions such as `test_tick` and
`test_set_alarm`; use it in a development environment, not as a bedside device
configuration. Its template firing/state sensors stand in for speaker output.
It does not exercise the custom HA integration, speech recognition, or TTS.

## 3. Actual S3 firmware in the emulator

```sh
esphome compile test/esp32-s3.yaml
python test/install_emulator.py
python test/run_emulator.py
```

The installer verifies a fixed SHA-256 checksum and installs only into
`.tools/esp-emu/`. It does not run a remote installation script or alter PATH.

The runner copies the merged factory image (bootloader + partition table + app)
to a temporary file. It boots that image with a 30-second emulator deadline and
an additional outer timeout. Scheduler failures, missing success markers, wrong
probe values, nonzero exits, and timeouts fail the command.

The first boot saves preferences probe `1` through ESPHome's real ESP32 NVS
backend. `--save-state` persists the emulated flash; a second emulator process
loads it and must report probe `2`. The original build image is never changed.
This proves clean restart persistence, not interrupted-flash-write recovery.
Logs are saved in `build/emulator-logs/`.

Optional runner arguments are `--firmware`, `--emulator`, and `--log-dir`.
`--firmware` must identify a merged factory image, not an OTA/application-only
binary. The S3 fixture intentionally has no networking, I²S audio, microphone,
or XMOS dependencies.

Espressif QEMU remains an alternative for future emulator compatibility work;
it is not a verified test backend in this repository. The working esp-emu
backend means QEMU is not required. Wokwi is optional and not part of CI.

## 4. Real device recipes (compile only)

```sh
python -m esphome compile test/voice-pe.yaml
python -m esphome compile test/satellite1.yaml
```

Both recipes keep the M3 alarm layer verified against two real device
packages. They share the wiring: the M3 blocks (alarm audio, volume,
dismissal, entities, the `set_alarm` action, the no-reboot policy) live in
[`packages/alarm-wiring.yaml`](../packages/alarm-wiring.yaml) — the same file
real users include with one line (see
[doc/installation.md](../doc/installation.md), Part 1 §3) — and both recipes
`!include` it.

### `test/voice-pe.yaml`

The full official Voice PE hardware recipe. A thin main config that pulls in
the vendored upstream recipe (git submodule `vendor/home-assistant-voice-pe`
at the pinned revision in
[`doc/versions.md`](../doc/versions.md)) via `packages:`, and adds:

- all 16 sound-file substitutions pinned to the submodule's `sounds/` copies;
- the `voice_alarm:` component (component id `alarm_clock`) bound to an SNTP
  `time:` platform (timezone from `!secret timezone`);
- Wi-Fi credentials via `!secret`;
- the M3 alarm wiring via `!include ../packages/alarm-wiring.yaml`;
- `external_components:` entries that resolve `voice_alarm` (this repository's
  `components/`) and `voice_kit` (the pinned submodule checkout) locally,
  shadowing the upstream git source the package still declares (`ref: dev`,
  fetched on every build — documented caveat).

### `test/satellite1.yaml`

The second verified alarm target: the upstream
[`FutureProofHomes/Satellite1-ESPHome`](https://github.com/FutureProofHomes/Satellite1-ESPHome)
package at the pinned release (see
[`doc/versions.md`](../doc/versions.md)), fetched from git at compile time
(package + external components). The recipe adds:

- a `logger` and an SNTP `time:` platform (the base package provides
  neither; the full upstream user config does);
- `esp32.framework.advanced.include_builtin_idf_components: [json,
  esp_http_server]` — the package's `satellite1_radar` component needs both,
  which ESPHome excludes by default and v0.2.1 does not request itself
  (fixed upstream in `v0.2.2-beta.0` via the same mechanism);
- the `voice_alarm:` component bound to the time source;
- the M3 alarm wiring via `!include ../packages/alarm-wiring.yaml`.

The wiring works identically on both devices through the same public hooks:
the package's internal `timer_ringing` switch (local tone on repeat, media
ducking, "stop" wake word, button stopping, LEDs, 15-minute self-stop) and
the `external_media_player` (persisted alarm volume for the duration of the
ring; the prior media volume is captured at ring start and restored exactly
once when the ring ends). Dismissal: the package's button (Voice PE center
button / Satellite1 action button) single click and the "stop" wake word turn
the switch off; a 2-second poll in the wiring package then calls
`dismiss_session()` so the scheduler converges to `idle`. Firmware-backed
entities: `Alarm State` (text sensor), `Alarm Ringing` (binary sensor),
`Alarm Snapshot` (text sensor), `Alarm Volume` (number, 0.1–1.0/step 0.01,
persisted independently of the schedule).

Credentials are not committed. Copy `test/secretstemplate.yaml` to
`test/secrets.yaml` and fill in `wifi_ssid`, `wifi_password`, and `timezone`
(`test/secrets.yaml` is gitignored); the `!secret` constructor resolves it
from the recipe's directory. The Satellite1 recipe only needs `timezone`
(it provisions Wi-Fi over BLE at setup time).

The first build downloads the ESP-IDF framework, the upstream packages, the
XMOS firmware images (Voice PE `v1.3.1`, Satellite1 `v1.0.3`), and the
MicroWakeWord models into the ESPHome cache; later builds are incremental.
This layer is a compile check: speaker output and preemption, buttons, and
wake-word recognition are covered by the hardware acceptance gates in
`doc/implementation-plan.md`.

## CI

Every push to `main` and every PR runs native checks, the pinned host build/API
test (including the `set_alarm` request/ack round trip), the pinned S3
build/emulator test, the HA integration pytest suite (config flow, entry
lifecycle, request/ack protocol, manifest/translation sanity), and
compile-only builds of both real-device recipes (Voice PE and Satellite1,
dummy secrets from `test/secretstemplate.yaml`). The emulator job uploads boot
logs and the factory image/ELF. Daily and manually dispatched compatibility
builds also compile against ESPHome stable, beta, and dev; those are compile
checks, not extra verified emulator combinations.

## Coverage still to implement

The scheduler, its versioned storage, and the M3 alarm layer (volume
persistence in the component; ringing, media-volume capture/restore, and
button/wake-word dismissal in the shared wiring package
[`packages/alarm-wiring.yaml`](../packages/alarm-wiring.yaml), compile-verified
against the Voice PE and Satellite1 packages) are implemented and proven where
possible in the layers above. The preferences probe remains fixture-only
storage separate from the alarm schedule. The custom-integration HA service,
voice intents, and live firing events arrive with M4/M5.

One-shot creation through the wire is implemented (schema version 2:
`days_mask` `0` plus the explicit `year`/`month`/`day` date fields), including
host e2e coverage of the accept and invalid-date paths. Add implementation
and regression tests for the HA `set_alarm`/`delete_alarm`/`stop`/
`list_alarms` service layer (M4) and the hardware acceptance cases (M3).
Follow the acceptance table in
[the implementation plan](../doc/implementation-plan.md).

Use the physical Voice PE and Satellite1 for sound/volume/preemption,
microphone mute, real wake-word recognition while ringing, Wi-Fi outages,
clock drift, and interrupted power/write testing. Injecting a wake-word
callback only tests its software path.

## References

- [ESPHome host platform](https://esphome.io/components/host/)
- [Espressif esp-emu](https://github.com/espressif/esp-emulator)
- [Espressif QEMU for ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/tools/qemu.html)
- [Wokwi ESP32 peripheral support](https://docs.wokwi.com/guides/esp32)
