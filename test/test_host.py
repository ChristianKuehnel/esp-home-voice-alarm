"""Run the compiled host fixture over the real ESPHome native API."""

import asyncio
from contextlib import contextmanager
import importlib.util
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

import pytest
from aioesphomeapi import APIClient, APIConnectionError

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = ROOT / "test/.esphome/build/voice-alarm-host-test/.pioenvs/voice-alarm-host-test/program"
PORT = 16053
MONDAY = 1791788400


def _load_alarm_protocol():
    # Load the protocol module by path: the owning package's __init__ imports
    # homeassistant, which is not part of the esphome test venv.
    path = ROOT / "custom_components" / "voice_alarm" / "alarm_protocol.py"
    spec = importlib.util.spec_from_file_location("voice_alarm_alarm_protocol", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


alarm_protocol = _load_alarm_protocol()


@contextmanager
def host_process(tmp_path):
    binary = Path(os.environ.get("VOICE_ALARM_HOST_BINARY", DEFAULT_BINARY)).resolve()
    assert binary.is_file(), "Compile test/host.yaml before running this test"
    # Fail clearly instead of accidentally connecting to someone else's server.
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", PORT))
    log_path = tmp_path / "host.log"
    with log_path.open("ab") as log:
        process = subprocess.Popen(
            [str(binary)], stdout=log, stderr=subprocess.STDOUT,
            env={**os.environ, "TZ": "UTC0", "ESPHOME_PREFDIR": str(tmp_path / "prefs")},
        )
        try:
            yield process
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            print(log_path.read_text(errors="replace"))


async def connect(process):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        assert process.poll() is None, "Host fixture exited during startup"
        client = APIClient("127.0.0.1", PORT, password=None)
        try:
            await client.connect(login=True)
            return client
        except APIConnectionError:
            await client.disconnect(force=True)
            await asyncio.sleep(0.1)
    raise AssertionError("Host API did not become available")


async def observe(client):
    entities, services = await client.list_entities_services()
    keys = {entity.name: entity.key for entity in entities}
    states = {}
    client.subscribe_states(lambda state: states.__setitem__(state.key, state))

    async def expect(name, value):
        key = keys[name]
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if key in states and states[key].state == value:
                return
            await asyncio.sleep(0.02)
        raise AssertionError(f"{name}: expected {value!r}, received {states.get(key)!r}")

    async def action(name, **data):
        service = next(service for service in services if service.name == name)
        await client.execute_service(service, data)

    return expect, action, services


async def exercise(tmp_path):
    with host_process(tmp_path) as process:
        client = await connect(process)
        try:
            expect, action, services = await observe(client)
            await expect("Test Persistence Probe", 1)
            await expect("Test Alarm State", "idle")
            for dismiss in ("test_button", "test_wake_word"):
                await action("test_clear")
                await action("test_tick", epoch=MONDAY - 60)
                await action("test_set_alarm", hour=7, minute=0, days_mask=1)
                await expect("Test Alarm State", "armed")
                await action("test_tick", epoch=MONDAY - 60)
                await expect("Test Ringing", False)
                await action("test_tick", epoch=MONDAY)
                await expect("Test Ringing", True)
                await action(dismiss)
                await expect("Test Ringing", False)

            # Disconnect/reconnect to the same running device, without reboot.
            await action("test_save_probe", value=41)
            await expect("Test Persistence Probe", 41)
            await client.disconnect()
            client = await connect(process)
            expect, _, _ = await observe(client)
            await expect("Test Persistence Probe", 41)
        finally:
            await client.disconnect()

    assert (tmp_path / "prefs/voice-alarm-host-test.prefs").is_file()
    with host_process(tmp_path) as process:
            client = await connect(process)
            try:
                expect, _, _ = await observe(client)
                # Boot increments the saved probe, proving the new process loaded it.
                await expect("Test Persistence Probe", 42)
            finally:
                await client.disconnect()
    output = (tmp_path / "host.log").read_text(errors="replace")
    assert "VOICE_ALARM_SMOKE_FAIL" not in output
    assert output.count("VOICE_ALARM_SMOKE_PASS") == 2


def test_host_api_dismissal_reconnect_and_preferences(tmp_path):
    asyncio.run(exercise(tmp_path))


# M1.4: the production set_alarm action answers with the structured
# acknowledgment. Rejected requests leave the schedule and revision
# unchanged; an accepted one advances both before the ack is sent.
async def contract(tmp_path):
    with host_process(tmp_path) as process:
        client = await connect(process)
        try:
            expect, _, services = await observe(client)
            await expect("Test Alarm Snapshot", "revision=0;hour=0;minute=0;days_mask=0;enabled=0")
            await expect("Test Alarm State", "idle")

            # Accepted: the ack carries the echoed request_id and the new revision.
            ack = await alarm_protocol.set_alarm(
                client,
                services,
                alarm_protocol.SetAlarmRequest(
                    request_id=9, expected_revision=0, hour=7, minute=30, days_mask=31
                ),
            )
            assert ack.status == "ok"
            assert ack.request_id == 9
            assert ack.revision == 1
            await expect(
                "Test Alarm Snapshot", "revision=1;hour=7;minute=30;days_mask=31;enabled=1"
            )
            await expect("Test Alarm State", "armed")

            # Stale: rejected with the authoritative revision; nothing changes.
            with pytest.raises(alarm_protocol.StaleRevisionError) as stale:
                await alarm_protocol.set_alarm(
                    client,
                    services,
                    alarm_protocol.SetAlarmRequest(
                        request_id=10, expected_revision=0, hour=8, minute=0, days_mask=1
                    ),
                )
            assert stale.value.authoritative_revision == 1
            await expect(
                "Test Alarm Snapshot", "revision=1;hour=7;minute=30;days_mask=31;enabled=1"
            )

            # Invalid field: device error message surfaces verbatim; nothing changes.
            with pytest.raises(
                alarm_protocol.VoiceAlarmCommandError, match="hour must be 0..23"
            ):
                await alarm_protocol.set_alarm(
                    client,
                    services,
                    alarm_protocol.SetAlarmRequest(
                        request_id=11, expected_revision=1, hour=24, minute=0, days_mask=1
                    ),
                )
            await expect(
                "Test Alarm Snapshot", "revision=1;hour=7;minute=30;days_mask=31;enabled=1"
            )
        finally:
            await client.disconnect()

    # Restart: the revision is durable; the schedule itself is not persisted
    # until M2, so the device boots empty at the persisted revision.
    with host_process(tmp_path) as process:
        client = await connect(process)
        try:
            expect, _, services = await observe(client)
            await expect("Test Alarm Snapshot", "revision=1;hour=0;minute=0;days_mask=0;enabled=0")
            ack = await alarm_protocol.set_alarm(
                client,
                services,
                alarm_protocol.SetAlarmRequest(
                    request_id=12, expected_revision=1, hour=8, minute=15, days_mask=127
                ),
            )
            assert ack.status == "ok"
            assert ack.revision == 2
            await expect(
                "Test Alarm Snapshot", "revision=2;hour=8;minute=15;days_mask=127;enabled=1"
            )
        finally:
            await client.disconnect()


def test_host_set_alarm_request_ack_round_trip(tmp_path):
    asyncio.run(contract(tmp_path))
