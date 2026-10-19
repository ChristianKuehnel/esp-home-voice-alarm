# Pinned Compatibility Matrix

Recorded 2026-10-19 on Linux x86_64. All CI layers and the HA test suite were
verified with exactly these versions; a clean checkout reproducing the commands
below reproduces every CI layer.

## Firmware development environment (`.venv`)

| Component | Pinned version | Source of truth |
|-----------|----------------|-----------------|
| CPython | 3.13.5 | `python3.13` |
| ESPHome | 2026.9.1 | `test/requirements.txt` |
| ESP-IDF (pulled by ESPHome, into `.tools`) | 5.5.5 | ESPHome release manifest |
| aioesphomeapi | 46.3.0 | `test/requirements.txt` |
| pytest | 8.4.2 | `test/requirements.txt` |
| esp-emu | 0.49.0 | `test/install_emulator.py` (checksum-pinned) |
| C++ compiler | g++ (Debian 14.2.0-19) | host; `CXX=clang++` also supported |

PlatformIO Core itself is installed under `.tools/platformio` by ESPHome on the
first compile; nothing toolchain-related is installed outside the repository.

## Home Assistant integration environment (`.venv-ha`)

| Component | Pinned version | Notes |
|-----------|----------------|-------|
| CPython | 3.13.5 | same interpreter line as the firmware env |
| homeassistant | 2026.2.3 | development baseline under test |
| pytest / pytest-asyncio | 8.4.2 / 1.4.0 | plain harness, no `pytest-homeassistant-custom-component` |
| aioesphomeapi | 43.14.0 | pinned by HA 2026.2.3's esphome component (note: lower than 46.3.0 in the firmware env) |
| esphome component chain | see `test/ha/requirements.txt` | bleak/habluetooth 5.8.0, habluetooth set, mutagen, hassil, etc. |

The HA env runs with `config.skip_pip = True`, so every runtime requirement of
the built-in components reachable from the `esphome` dependency is installed
pinned up front; the pins live in `test/ha/requirements.txt`.

## Upstream Voice PE firmware package

| Component | Pinned revision |
|-----------|-----------------|
| Package repo | [`esphome/home-assistant-voice-pe`](https://github.com/esphome/home-assistant-voice-pe), branch `dev` |
| Revision | `d4e6fa43d6a1a7d342b7d4d403567e06516719de` (2026-09-17, "Add hardware design files (#5)") |
| Vendoring | git submodule at `vendor/home-assistant-voice-pe`, checkout pinned to the revision above |

Voice PE recipes in this repository must reference the submodule checkout
until a newer revision is re-verified. The recipe `test/voice-pe.yaml`
compiles for the official Voice PE target on the pinned ESPHome 2026.9.1
(verified 2026-10-19, Linux x86_64). The package still declares the upstream
git source for `voice_kit` (`ref: dev`, fetched on every build); the local
submodule entry shadows it, so upstream drift cannot change what compiles.

## Clean-environment reproduction

```sh
# Firmware env (native + host + emulator layers)
python3.13 -m venv .venv
. .venv/bin/activate
python -m pip install -r test/requirements.txt
export PLATFORMIO_CORE_DIR="$PWD/.tools/platformio"
export XDG_CACHE_HOME="$PWD/.tools/cache"
python3 test/run_native.py
python -m esphome compile test/host.yaml && python -m pytest -q test/test_host.py
python -m esphome compile test/esp32-s3.yaml
python test/install_emulator.py && python test/run_emulator.py
# Real Voice PE recipe (compile check only; needs local secrets, see test/README.md §4)
cp test/secretstemplate.yaml test/secrets.yaml # fill in wifi_ssid/wifi_password/timezone
python -m esphome compile test/voice-pe.yaml
deactivate

# HA integration env (flow + entry lifecycle suite)
python3.13 -m venv .venv-ha
. .venv-ha/bin/activate
python -m pip install -r test/ha/requirements.txt
python -m pytest -q test/ha/
```

The full per-layer documentation and failure modes are in
[`test/README.md`](../test/README.md).

## Verification status

- Verified: Linux x86_64 (Debian/Ubuntu family g++ 14.x).
- Not yet verified: other OS/arch combinations (esp-emu ships x86_64/ARM64
  Linux and macOS ARM64 build toolchains; the test matrix has only been run on
  Linux x86_64).
- `test/ha/requirements.txt` and `test/requirements.txt` are the only
  version authorities. Do not pin versions only here.
