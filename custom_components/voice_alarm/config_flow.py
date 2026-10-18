"""Config flow for Voice Alarm Clock."""

from __future__ import annotations

import logging
from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.const import CONF_DEVICE_ID
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers import device_registry as dr, selector

from . import DOMAIN

_LOGGER = logging.getLogger(__package__)

ESPHOME_DOMAIN = "esphome"


def _device_label(device: dr.DeviceEntry) -> str:
    """Return a human readable label for a device registry entry."""
    return device.name_by_user or device.name or device.id


def _esphome_devices(hass) -> dict[str, dr.DeviceEntry]:
    """Return ESPHome devices from the device registry, keyed by device id."""
    registry = dr.async_get(hass)
    devices: dict[str, dr.DeviceEntry] = {}
    for device in registry.devices.values():
        for entry_id in device.config_entries:
            entry = hass.config_entries.async_get_entry(entry_id)
            if entry is not None and entry.domain == ESPHOME_DOMAIN:
                devices[device.id] = device
                break
    return devices


class VoiceAlarmConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config entry for Voice Alarm Clock."""

    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Let the user pick the ESPHome device that runs the alarm."""
        errors: dict[str, str] = {}
        devices = _esphome_devices(self.hass)
        if not devices:
            return self.async_abort(reason="no_devices_found")

        if user_input is not None:
            device_id = user_input[CONF_DEVICE_ID]
            if device_id not in devices:
                errors["base"] = "unknown_device"
            else:
                await self.async_set_unique_id(device_id)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(
                    title=_device_label(devices[device_id]),
                    data={CONF_DEVICE_ID: device_id},
                )

        device_options = [
            {"value": device.id, "label": _device_label(device)}
            for device in devices.values()
        ]
        schema = vol.Schema(
            {vol.Required(CONF_DEVICE_ID): selector.SelectSelector(
                {"options": device_options}
            )}
        )
        return self.async_show_form(
            step_id="user",
            data_schema=schema,
            errors=errors,
        )
