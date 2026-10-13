"""Boot the compiled S3 image twice, checking scheduler assertions and NVS."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIRMWARE = ROOT / "test/.esphome/build/voice-alarm-s3-test/build/firmware.factory.bin"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    parser.add_argument("--emulator", type=Path, default=ROOT / ".tools/esp-emu/esp-emu")
    parser.add_argument("--log-dir", type=Path, default=ROOT / "build/emulator-logs")
    args = parser.parse_args()
    if not args.firmware.is_file():
        parser.error("Compile test/esp32-s3.yaml first (a merged factory image is required)")
    if not args.emulator.is_file():
        parser.error("Run python test/install_emulator.py first, or supply --emulator")
    args.log_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="voice-alarm-flash-") as directory:
        # --save-state modifies the image. Never mutate the build artifact.
        flash = Path(directory) / "flash.bin"
        shutil.copy2(args.firmware, flash)
        for boot in (1, 2):
            marker = f"VOICE_ALARM_SMOKE_PASS probe={boot}"
            command = [str(args.emulator.resolve()), "--chip", "esp32s3",
                       "--firmware", str(flash), "--save-state", "--timeout", "30s",
                       "--net", "user,restrict=yes", "--exit-on", marker]
            try:
                result = subprocess.run(
                    command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, errors="replace", timeout=45,
                    env={**os.environ, "RUST_LOG": "warn", "NO_COLOR": "1"},
                )
            except subprocess.TimeoutExpired as error:
                output = error.stdout or b""
                if isinstance(output, bytes):
                    output = output.decode(errors="replace")
                (args.log_dir / f"boot-{boot}.log").write_text(output)
                raise SystemExit(f"Emulator boot {boot} timed out; see {args.log_dir}")
            (args.log_dir / f"boot-{boot}.log").write_text(result.stdout)
            probes = re.findall(r"VOICE_ALARM_SMOKE_PASS probe=(\d+)", result.stdout)
            if result.returncode or probes != [str(boot)] or "VOICE_ALARM_SMOKE_FAIL" in result.stdout:
                print(result.stdout)
                raise SystemExit(f"Emulator boot {boot} failed (exit {result.returncode})")
            print(f"Boot {boot}: scheduler assertions passed; persisted probe={boot}")
    print("VOICE_ALARM_EMULATOR_PASS: Xtensa firmware and NVS reload verified")


if __name__ == "__main__":
    main()
