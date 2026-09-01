"""Config flow for Voice Alarm Clock."""

from __future__ import annotations

import logging
from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import FlowResult
from homeassistant.exceptions import HomeAssistantError

from . import DOMAIN

_LOGGER = logging.getLogger(__package__)

STEP_USER_DATA_SCHEMA = vol.Schema({
    vol.Required("device_id", default=""): str,
    vol.Optional("volume_default", default=0.7): vol.Coerce(float),
})


async def async_step_user(hass: HomeAssistant, user_input: dict[str, Any] | None = None) -> FlowResult:
    """Handle a user config entry."""
    if user_input is None:
        return self.async_show_form(step_id="user", data_schema=STEP_USER_DATA_SCHEMA)

    return self.async_create_entry(
        title="Voice Alarm Clock",
        data=user_input,
    )


class VoiceAlarmConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config entry for Voice Alarm Clock."""

    VERSION = 1

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> FlowResult:
        return await async_step_user(self.hass, user_input)


class OptionsFlow(config_entries.OptionsFlow):
    """Handle options."""

    def __init__(self, entry: config_entries.ConfigEntry) -> None:
        self.entry = entry

    async def async_step_init(self, user_input: dict[str, Any] | None = None) -> FlowResult:
        if user_input is None:
            return self.async_show_form(
                step_id="user",
                data_schema=vol.Schema({
                    vol.Optional("volume_default", default=self.entry.options.get("volume_default", 0.7)): vol.Coerce(float),
                }),
            )

        return self.async_create_entry(data=user_input)