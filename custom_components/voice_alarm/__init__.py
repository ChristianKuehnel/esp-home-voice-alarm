"""Voice Alarm Clock — Home Assistant Custom Integration."""

from __future__ import annotations

import logging

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_DEVICE_ID
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryNotReady
from homeassistant.helpers import device_registry as dr

DOMAIN = "voice_alarm"

_LOGGER = logging.getLogger(__package__)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up Voice Alarm Clock from a config entry."""
    registry = dr.async_get(hass)
    device = registry.devices.get(entry.data[CONF_DEVICE_ID])
    if device is None:
        raise ConfigEntryNotReady(
            "The configured ESPHome device is not in the device registry yet. "
            "Setup will be retried."
        )

    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = device.id
    entry.async_on_unload(entry.add_update_listener(async_reload_entry))
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    hass.data.get(DOMAIN, {}).pop(entry.entry_id, None)
    return True


async def async_reload_entry(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Reload a config entry."""
    await hass.config_entries.async_unload(entry.entry_id)
    await hass.config_entries.async_setup(entry.entry_id)
