"""Compile and run the production scheduler without ESPHome or an ESP32 SDK."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="voice-alarm-native-") as directory:
        build = Path(directory)
        # Match ESPHome's include tree without mocking framework headers.
        include = build / "esphome/components/voice_alarm"
        include.mkdir(parents=True)
        shutil.copy2(ROOT / "components/voice_alarm/alarm_state.h", include)
        shutil.copy2(ROOT / "components/voice_alarm/alarm_request.h", include)
        executable = build / "scheduler-tests"
        subprocess.run(
            [*shlex.split(os.environ.get("CXX", "g++")), "-std=c++17",
             "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-I", str(build), "-I", str(ROOT / "test/fixtures"),
             str(ROOT / "test/native/main.cpp"),
             str(ROOT / "components/voice_alarm/alarm_state.cpp"),
             str(ROOT / "components/voice_alarm/alarm_request.cpp"),
             "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True, env={**os.environ, "TZ": "UTC0"})


if __name__ == "__main__":
    main()
