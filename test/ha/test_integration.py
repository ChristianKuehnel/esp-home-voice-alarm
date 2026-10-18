"""Tests for the Voice Alarm Clock config flow and entry lifecycle."""

from __future__ import annotations

import pytest
from homeassistant import config_entries as ce
from homeassistant.const import CONF_DEVICE_ID
from homeassistant.data_entry_flow import FlowResultType, InvalidData

DOMAIN = "voice_alarm"


@pytest.mark.asyncio
async def test_flow_aborts_when_no_esphome_devices(hass):
    """With no ESPHome device in the registry the flow must not start."""
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": "user"}
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "no_devices_found"


@pytest.mark.asyncio
async def test_flow_shows_form_listing_esphome_devices(hass, esphome_device):
    """The user step offers the ESPHome devices from the registry."""
    device = esphome_device(name="Voice PE", identifier="va-dev-1")

    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": "user"}
    )
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "user"

    # The form's only field is a select selector offering the device(s).
    key, validator = next(iter(result["data_schema"].schema.items()))
    assert key.schema == CONF_DEVICE_ID
    options = validator.config["options"]
    assert options == [{"value": device.id, "label": "Voice PE"}]

    # Continuing the flow with the device creates an entry.
    result2 = await hass.config_entries.flow.async_configure(
        result["flow_id"], {CONF_DEVICE_ID: device.id}
    )
    assert result2["type"] is FlowResultType.CREATE_ENTRY
    assert result2["result"].data == {CONF_DEVICE_ID: device.id}
    assert result2["result"].title == "Voice PE"


@pytest.mark.asyncio
async def test_flow_sets_unique_id_and_prevents_duplicates(hass, esphome_device):
    """Each registry device may produce exactly one config entry."""
    device = esphome_device(name="Voice PE", identifier="va-dev-2")

    result = await hass.config_entries.flow.async_init(
        DOMAIN,
        context={"source": "user"},
        data={CONF_DEVICE_ID: device.id},
    )
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["result"].unique_id == device.id

    result2 = await hass.config_entries.flow.async_init(
        DOMAIN,
        context={"source": "user"},
        data={CONF_DEVICE_ID: device.id},
    )
    assert result2["type"] is FlowResultType.ABORT
    assert result2["reason"] == "already_configured"


@pytest.mark.asyncio
async def test_flow_rejects_non_esphome_device(hass, esphome_device, foreign_device):
    """A device that does not belong to ESPHome cannot be configured."""
    esphome_device(name="Voice PE", identifier="va-dev-3")
    foreign = foreign_device()

    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": "user"}
    )
    assert result["type"] is FlowResultType.FORM

    # The foreign device is not offered in the selector.
    key, validator = next(iter(result["data_schema"].schema.items()))
    offered = [option["value"] for option in validator.config["options"]]
    assert foreign.id not in offered

    # Submitting it is rejected by schema validation.
    with pytest.raises(InvalidData):
        await hass.config_entries.flow.async_configure(
            result["flow_id"], {CONF_DEVICE_ID: foreign.id}
        )


@pytest.mark.asyncio
async def test_setup_and_unload_entry(hass, esphome_device, va_entry):
    """A valid entry loads and unloads cleanly."""
    device = esphome_device(name="Voice PE", identifier="va-dev-4")
    entry = va_entry(device.id)

    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ce.ConfigEntryState.LOADED
    assert hass.data[DOMAIN][entry.entry_id] == device.id

    assert await hass.config_entries.async_unload(entry.entry_id)
    assert entry.state is ce.ConfigEntryState.NOT_LOADED
    assert entry.entry_id not in hass.data.get(DOMAIN, {})


@pytest.mark.asyncio
async def test_setup_retries_when_device_is_missing(hass, va_entry):
    """If the bound device vanished, setup defers instead of failing."""
    entry = va_entry("device-that-does-not-exist")

    assert not await hass.config_entries.async_setup(entry.entry_id)
    assert entry.state is ce.ConfigEntryState.SETUP_RETRY
