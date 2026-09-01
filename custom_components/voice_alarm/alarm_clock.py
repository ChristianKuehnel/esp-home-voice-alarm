"""Voice Alarm Clock entity definitions."""

from __future__ import annotations

from homeassistant.components.number import NumberEntity
from homeassistant.core import HomeAssistant
from homeassistant.config_entries import ConfigEntry


class VolumeEntity(NumberEntity):
    """Volume control entity for alarm clock."""

    _attr_device_class = "volume"
    _attr_native_min_value = 0.1
    _attr_native_max_value = 1.0
    _attr_native_step = 0.1
    _attr_native_value = 0.7  # Default volume (ADR-005)
    _attr_should_poll = False

    def __init__(self, unique_id: str, device_id: str):
        self._attr_name = "Alarm Clock Volume"
        self._attr_unique_id = unique_id
        self._device_id = device_id
        self._attr_device_info = {
            "identifiers": {("voice_alarm", device_id)},
            "name": "Voice Alarm Clock",
            "manufacturer": "Christian Kuehnel",
            "model": "Voice Alarm Clock (custom)",
        }

    async def async_set_native_value(self, value: float) -> None:
        """Set volume."""
        self._attr_native_value = value
        self.async_write_ha_state()

    async def async_added_to_hass(self) -> None:
        """Entity added to HA."""
        state = await self.async_get_last_state()
        if state is not None:
            self._attr_native_value = float(state.state)