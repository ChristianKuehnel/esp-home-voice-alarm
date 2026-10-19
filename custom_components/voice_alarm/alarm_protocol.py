"""Request/acknowledgment protocol between the voice_alarm custom integration
and one ESPHome device (``set_alarm`` user action, ADR-009).

The device answers every ``set_alarm`` call with a structured acknowledgment
(JSON payload: ``schema_version``, ``request_id``, ``status``, ``revision``).
Commands are never retried blindly: a stale request is rejected by the device
and the caller must refresh device state and re-issue with the authoritative
revision.

This module talks to a connected ``APIClient`` only and depends on
``aioesphomeapi`` alone, so it runs in both the HA runtime venv and the host
e2e venv.
"""

from __future__ import annotations

import json
from dataclasses import dataclass

from aioesphomeapi import APIClient, ExecuteServiceResponse, UserService
from aioesphomeapi.core import APIConnectionCancelledError, TimeoutAPIError

SCHEMA_VERSION = 1
SERVICE_SET_ALARM = "set_alarm"


class VoiceAlarmCommandError(Exception):
    """The device rejected the command or answered inconsistently."""


class UnsupportedFirmwareError(VoiceAlarmCommandError):
    """The device has no request/ack capable set_alarm action yet.

    Raised when the action is missing, no response payload arrives, or the
    payload schema is not understood. The caller must surface this to the
    user and must not retry the command.
    """


class StaleRevisionError(VoiceAlarmCommandError):
    """The device did not apply the command (expected revision mismatch)."""

    def __init__(self, message: str, *, authoritative_revision: int) -> None:
        super().__init__(message)
        self.authoritative_revision = authoritative_revision


@dataclass(frozen=True)
class SetAlarmRequest:
    request_id: int
    expected_revision: int
    hour: int
    minute: int
    days_mask: int


@dataclass(frozen=True)
class CommandAck:
    status: str
    request_id: int
    revision: int
    error_message: str = ""


def _reject(message: str) -> VoiceAlarmCommandError:
    """Build a command error carrying the device-side message."""
    return VoiceAlarmCommandError(message or "set_alarm command rejected")


async def set_alarm(
    client: APIClient,
    services: list[UserService],
    request: SetAlarmRequest,
    *,
    timeout: float = 10.0,
) -> CommandAck:
    """Send a set_alarm command and wait for the structured acknowledgment.

    ``services`` is the device's cached user-service list (as returned by
    ``APIClient.list_entities_services()``). Raises
    :class:`UnsupportedFirmwareError` for devices without the M1.4 action,
    :class:`StaleRevisionError` when the expected revision did not match,
    and :class:`VoiceAlarmCommandError` for any other device-side rejection.
    """
    service = next((s for s in services if s.name == SERVICE_SET_ALARM), None)
    if service is None:
        raise UnsupportedFirmwareError(
            f"device does not expose the '{SERVICE_SET_ALARM}' user action; "
            "update the firmware to a request/ack build"
        )

    try:
        response = await client.execute_service(
            service,
            {
                "request_id": request.request_id,
                "expected_revision": request.expected_revision,
                "hour": request.hour,
                "minute": request.minute,
                "days_mask": request.days_mask,
            },
            return_response=True,
            timeout=timeout,
        )
    except APIConnectionCancelledError as err:
        raise VoiceAlarmCommandError("connection closed while waiting for the acknowledgment") from err
    except TimeoutAPIError as err:
        raise UnsupportedFirmwareError(
            "no acknowledgment within the timeout; the firmware is not "
            "request/ack capable. Refresh device state; do not repeat the command"
        ) from err
    if response is None:
        raise UnsupportedFirmwareError(
            "device answered without a structured response payload; "
            "update the firmware to a request/ack build"
        )
    return _interpret(response)


def _interpret(response: ExecuteServiceResponse) -> CommandAck:
    try:
        payload = json.loads(bytes(response.response_data))
    except (TypeError, ValueError):
        payload = None
    if not isinstance(payload, dict) or payload.get("schema_version") is None:
        raise UnsupportedFirmwareError(
            "response payload is missing or has no schema_version; "
            "update the firmware to a request/ack build"
        )
    try:
        schema_version = int(payload["schema_version"])
    except (TypeError, ValueError) as err:
        raise UnsupportedFirmwareError("response payload has an invalid schema_version") from err
    if schema_version != SCHEMA_VERSION:
        raise UnsupportedFirmwareError(
            f"unsupported response schema version {schema_version} (expected {SCHEMA_VERSION})"
        )
    try:
        ack = CommandAck(
            status=str(payload.get("status", "")),
            request_id=int(payload.get("request_id", 0)),
            revision=int(payload.get("revision", 0)),
            error_message=response.error_message,
        )
    except (TypeError, ValueError) as err:
        raise VoiceAlarmCommandError("response payload is malformed") from err

    if response.success and ack.status == "ok":
        return ack
    if ack.status == "stale_revision":
        raise StaleRevisionError(
            ack.error_message or "expected revision does not match the device revision",
            authoritative_revision=ack.revision,
        )
    raise _reject(ack.error_message or ack.status)
