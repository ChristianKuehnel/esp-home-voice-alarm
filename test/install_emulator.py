"""Install a checksum-pinned esp-emu binary in .tools, never into the user's PATH."""

import hashlib
from pathlib import Path
import platform
import shutil
import tarfile
import tempfile
from urllib.request import urlopen

VERSION = "0.49.0"
ASSETS = {
    ("Linux", "x86_64"): ("x86_64-unknown-linux-gnu", "100d656d33a2c26b4efd38e009b92e68914452105585187d3b8807ee87041ffc"),
    ("Linux", "aarch64"): ("aarch64-unknown-linux-gnu", "6b8290b34001898ab0e33d5ac72e04c1d0c76e3369a64d94e1d0a8c0d43912f3"),
    ("Darwin", "arm64"): ("aarch64-apple-darwin", "3982fd2472be4e28524b98e0de8179db00ef726e7fa511a6d2f571a2da3ced3c"),
}
ROOT = Path(__file__).resolve().parents[1]


def main():
    try:
        target, expected = ASSETS[platform.system(), platform.machine()]
    except KeyError:
        raise SystemExit("No pinned esp-emu asset for this host; use Linux/WSL or the QEMU runner")
    url = f"https://github.com/espressif/esp-emulator/releases/download/v{VERSION}/esp-emu-{VERSION}-{target}.tar.gz"
    with tempfile.TemporaryDirectory(prefix="voice-alarm-emulator-") as directory:
        archive = Path(directory) / "emulator.tar.gz"
        with urlopen(url, timeout=60) as source, archive.open("wb") as output:
            shutil.copyfileobj(source, output)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != expected:
            raise SystemExit(f"Emulator checksum mismatch: {digest}")
        destination = ROOT / ".tools/esp-emu/esp-emu"
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            members = [m for m in tar.getmembers() if m.isfile() and Path(m.name).name == "esp-emu"]
            if len(members) != 1:
                raise SystemExit("Unexpected emulator archive layout")
            # Copy only the executable, without extracting arbitrary paths/links.
            with tar.extractfile(members[0]) as source, destination.open("wb") as output:
                shutil.copyfileobj(source, output)
        destination.chmod(0o755)
        print(f"Installed esp-emu {VERSION}: {destination}")


if __name__ == "__main__":
    main()
