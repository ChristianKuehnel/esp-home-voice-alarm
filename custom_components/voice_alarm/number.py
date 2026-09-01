"""Volume number platform for Voice Alarm Clock."""

from __future__ import annotations

from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.config_entries import ConfigEntry

from .alarm_clock import VolumeEntity

async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up number entities."""
    device_id = entry.data.get("device_id", "unknown")
    async_add_entities([
        VolumeEntity(
            unique_id=f"{entry.entry_id}_volume",
            device_id=device_id,
        ),
    ])