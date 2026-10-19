"""Unit tests for the voice_alarm set_alarm request/ack protocol."""

from __future__ import annotations

import json

import pytest
from aioesphomeapi import ExecuteServiceResponse, UserService
from aioesphomeapi.core import TimeoutAPIError

from custom_components.voice_alarm.alarm_protocol import (
    SCHEMA_VERSION,
    SERVICE_SET_ALARM,
    CommandAck,
    SetAlarmRequest,
    StaleRevisionError,
    UnsupportedFirmwareError,
    VoiceAlarmCommandError,
    set_alarm,
)


class FakeClient:
    """Returns canned responses and records the executed service calls."""

    def __init__(self, response=None, error: Exception | None = None):
        self.response = response
        self.error = error
        self.calls: list[tuple[str, dict, dict]] = []

    async def execute_service(self, service, data, *, return_response=None, timeout=None):
        self.calls.append((service.name, dict(data), {"return_response": return_response, "timeout": timeout}))
        if self.error is not None:
            raise self.error
        return self.response


def _service(name=SERVICE_SET_ALARM, key=1):
    return UserService(name=name, key=key, args=[], supports_response="optional")


def _ack_payload(schema_version=SCHEMA_VERSION, status="ok", request_id=7, revision=1):
    return json.dumps(
        {"schema_version": schema_version, "status": status, "request_id": request_id, "revision": revision}
    ).encode()


def _request(**overrides):
    base = {"request_id": 7, "expected_revision": 0, "hour": 7, "minute": 30, "days_mask": 31}
    base.update(overrides)
    return SetAlarmRequest(**base)


@pytest.mark.asyncio
async def test_set_alarm_ack():
    """A successful command yields an ack with the new authoritative revision."""
    client = FakeClient(
        ExecuteServiceResponse(call_id=1, success=True, response_data=_ack_payload())
    )

    ack = await set_alarm(client, [_service()], _request())

    assert isinstance(ack, CommandAck)
    assert ack.status == "ok"
    assert ack.request_id == 7
    assert ack.revision == 1
    # The request fields are passed as the action variables, response requested.
    (name, data, opts), = client.calls
    assert name == SERVICE_SET_ALARM
    assert data == {"request_id": 7, "expected_revision": 0, "hour": 7, "minute": 30, "days_mask": 31}
    assert opts["return_response"] is True


@pytest.mark.asyncio
async def test_missing_action_is_unsupported_firmware():
    """A device without the set_alarm action is rejected, not retried."""
    client = FakeClient(ExecuteServiceResponse(success=True))

    with pytest.raises(UnsupportedFirmwareError, match="does not expose"):
        await set_alarm(client, [_service(name="some_other_action")], _request())
    assert client.calls == []


@pytest.mark.asyncio
async def test_timeout_is_unsupported_firmware():
    """No ack within the timeout means a non-capable firmware: refresh, don't repeat."""
    client = FakeClient(error=TimeoutAPIError("timed out"))

    with pytest.raises(UnsupportedFirmwareError, match="timeout"):
        await set_alarm(client, [_service()], _request(), timeout=2.0)


@pytest.mark.asyncio
async def test_no_payload_is_unsupported_firmware():
    """A response without the structured payload predates request/ack."""
    client = FakeClient(None)

    with pytest.raises(UnsupportedFirmwareError, match="payload"):
        await set_alarm(client, [_service()], _request())


@pytest.mark.asyncio
async def test_payload_without_schema_is_unsupported():
    """A response that has data but no schema_version is an older firmware."""
    client = FakeClient(ExecuteServiceResponse(success=True, response_data=b"{}"))

    with pytest.raises(UnsupportedFirmwareError, match="schema_version"):
        await set_alarm(client, [_service()], _request())


@pytest.mark.asyncio
async def test_unknown_schema_version_is_unsupported():
    """An unknown schema version must not be silently accepted."""
    client = FakeClient(
        ExecuteServiceResponse(
            success=True, response_data=_ack_payload(schema_version=SCHEMA_VERSION + 1)
        )
    )

    with pytest.raises(UnsupportedFirmwareError, match="schema version"):
        await set_alarm(client, [_service()], _request())


@pytest.mark.asyncio
async def test_stale_revision_reports_authoritative_revision():
    """A rejected stale command surfaces the device's current revision."""
    client = FakeClient(
        ExecuteServiceResponse(
            call_id=1,
            success=False,
            error_message="stale",
            response_data=_ack_payload(status="stale_revision", revision=3),
        )
    )

    with pytest.raises(StaleRevisionError) as exc:
        await set_alarm(client, [_service()], _request(expected_revision=2))
    assert exc.value.authoritative_revision == 3
    assert isinstance(exc.value, VoiceAlarmCommandError)


@pytest.mark.asyncio
async def test_invalid_field_surfaces_device_message():
    """Any other device-side rejection carries the device error message."""
    client = FakeClient(
        ExecuteServiceResponse(
            call_id=1,
            success=False,
            error_message="hour must be 0..23",
            response_data=_ack_payload(status="invalid_hour", revision=1),
        )
    )

    with pytest.raises(VoiceAlarmCommandError, match="hour must be 0..23") as exc:
        await set_alarm(client, [_service()], _request(hour=24))
    assert not isinstance(exc.value, (UnsupportedFirmwareError, StaleRevisionError))


@pytest.mark.asyncio
async def test_malformed_payload_is_unsupported_firmware():
    """A non-JSON payload has no protocol: treat it as pre-request/ack firmware."""
    client = FakeClient(ExecuteServiceResponse(success=False, response_data=b"not-json"))

    with pytest.raises(UnsupportedFirmwareError, match="missing or has no schema_version"):
        await set_alarm(client, [_service()], _request())
