# Testing without a Voice PE

Run commands from the repository root. The test setup executes the production
C++ scheduler in three environments. It does not emulate the complete Voice PE.

| Layer | Entry point | Coverage today |
|-------|-------------|----------------|
| Native C++ | `python3 test/run_native.py` | Deterministic idle/set/fire/stop/cancel checks and every weekday bit, with no ESPHome dependency |
| ESPHome host | `test/host.yaml` + `test/test_host.py` | Compiled ESPHome component, real native API, injected button/wake-word callbacks, disconnect/reconnect, and file-backed preferences across process restarts |
| ESP32-S3 | `test/esp32-s3.yaml` + `test/run_emulator.py` | Actual Xtensa/ESP-IDF binary boot, the same scheduler assertions, ESPHome preferences backed by NVS, and a second boot from saved flash |
 | HA integration | `test/ha/` (pytest suite) | Config flow + entry lifecycle against a pinned `homeassistant` build: registry validation, unload, and duplicate prevention |
 | Voice PE recipe | `test/voice-pe.yaml` (compile only) | Full official Voice PE hardware recipe: vendored upstream package + `voice_alarm`, NTP, and the Wi-Fi/OTA/API reconnect workarounds — compiled, not emulated |

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
treated as errors and runs `test/fixtures/scheduler_checks.h`. Output is
`VOICE_ALARM_NATIVE_PASS`. Build products live in a temporary directory.

The core takes an explicit wall-clock timestamp; tests advance time without
sleeping. The runner fixes `TZ=UTC0`. No ESPHome headers or behavior are mocked.
Add future scheduler cases here so the same assertions also run in the firmware.

## 2. Host/API integration

```sh
esphome compile test/host.yaml
python -m pytest -q test/test_host.py
```

The test starts and cleans up the compiled process itself. It uses TCP port
16053, refuses to proceed if that port is already in use, and connects using
`aioesphomeapi`. It asserts firing and dismissal through two independently
injected inputs, reconnects to the running process, writes a preferences probe,
then restarts and verifies the restored value. Preferences and logs are isolated
in pytest's temporary directory. Failure output includes the process log.

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

## 4. Real Voice PE recipe (compile only)

```sh
python -m esphome compile test/voice-pe.yaml
```

`test/voice-pe.yaml` is the full official Voice PE hardware recipe. It is a
thin main config that pulls in the vendored upstream recipe (git submodule
`vendor/home-assistant-voice-pe` at the pinned revision in
[`doc/versions.md`](../doc/versions.md)) via `packages:`, and adds:

- all 16 sound-file substitutions pinned to the submodule's `sounds/` copies;
- the `voice_alarm:` component (component id `alarm_clock`) bound to a `time`
  source;
- an SNTP `time:` platform with the timezone from `!secret timezone`;
- Wi-Fi credentials via `!secret` and `reboot_timeout: 0s` on both `wifi:` and
  `api:`, disabling disconnect-triggered reboots per ADR-009:82;
- `external_components:` entries that resolve `voice_alarm` (this repository's
  `components/`) and `voice_kit` (the pinned submodule checkout) locally,
  shadowing the upstream git source the package still declares (`ref: dev`,
  fetched on every build — documented caveat).

Credentials are not committed. Copy `test/secretstemplate.yaml` to
`test/secrets.yaml` and fill in `wifi_ssid`, `wifi_password`, and `timezone`
(`test/secrets.yaml` is gitignored); the `!secret` constructor resolves it from
the recipe's directory.

The first build downloads the ESP-IDF framework, the XMOS voice-kit firmware
(`v1.3.1`), and the MicroWakeWord models into `.tools/cache`; later builds are
incremental. This layer is a compile check: speaker output and preemption,
buttons, and wake-word recognition are covered by the hardware acceptance
gates in `doc/implementation-plan.md`.

## CI

Every push to `main` and every PR runs native checks, the pinned host build/API
test, and the pinned S3 build/emulator test. The emulator job uploads boot logs
and the factory image/ELF. Daily and manually dispatched compatibility builds
also compile against ESPHome stable, beta, and dev; those are compile checks,
not extra verified emulator combinations.

## Coverage still to implement

The scheduler remains the original single-alarm scaffold with an injectable
clock. This test setup deliberately does not claim completion of ADR-009.
The preferences probe is fixture-only storage, not persistent alarm schedules.
The test API is not the planned `voice_alarm` HA service/acknowledgment contract.

Add implementation and regression tests for dated one-shots, rearming repeating
alarms, crossing missed deadlines, invalid clocks, DST, five-minute catch-up,
multiple entries/shared sessions, monotonic 15-minute timeout, versioned alarm
storage, failed saves, and restart deduplication. Follow the acceptance table in
[the implementation plan](../doc/implementation-plan.md).

Use the physical Voice PE for sound/volume/preemption, microphone mute, real
wake-word recognition while ringing, Wi-Fi outages, clock drift, and interrupted
power/write testing. Injecting a wake-word callback only tests its software path.

## References

- [ESPHome host platform](https://esphome.io/components/host/)
- [Espressif esp-emu](https://github.com/espressif/esp-emulator)
- [Espressif QEMU for ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/tools/qemu.html)
- [Wokwi ESP32 peripheral support](https://docs.wokwi.com/guides/esp32)
