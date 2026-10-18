"""Minimal test harness for the Voice Alarm Clock custom integration.

Runs the custom component inside a real (but local, network-free)
Home Assistant instance, so config flow, entry setup/unload and device
registry behavior are tested against the pinned HA release.
"""

from __future__ import annotations

import asyncio
import sys
import types
from pathlib import Path

import pytest
import pytest_asyncio
from homeassistant import core as ha, loader
from homeassistant.bootstrap import async_from_config_dict
from homeassistant.config_entries import (
    ConfigEntry,
    ConfigEntryDisabler,
    ConfigEntryState,
)
from homeassistant.const import CONF_DEVICE_ID
from homeassistant.helpers import device_registry as dr

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

DOMAIN = "voice_alarm"


def _make_config_entry(
    hass,
    domain,
    data,
    unique_id=None,
    *,
    title="Test Entry",
    source="user",
    version=1,
    disabled=False,
):
    """Create and register a config entry without starting its setup."""
    entry = ConfigEntry(
        version=version,
        minor_version=1,
        domain=domain,
        title=title,
        data=data,
        source=source,
        unique_id=unique_id,
        discovery_keys=types.MappingProxyType({}),
        options={},
        subentries_data=None,
    )
    entry.setup_lock = asyncio.Lock()
    if disabled:
        # Disabled entries are skipped when their component is set up,
        # so these fake owners never attempt real hardware/zeroconf I/O.
        entry.disabled_by = ConfigEntryDisabler.USER
    # ConfigEntryItems keeps the domain/unique_id indexes automatically.
    hass.config_entries._entries[entry.entry_id] = entry
    return entry


def _make_esphome_device(hass, name, identifier, title=None):
    """Register an ESPHome device in the device registry."""
    esphome_entry = _make_config_entry(
        hass,
        "esphome",
        {"host": "10.0.0.10"},
        unique_id=identifier,
        title=title or name,
        disabled=True,
    )
    registry = dr.async_get(hass)
    device = registry.async_get_or_create(
        config_entry_id=esphome_entry.entry_id,
        connections=set(),
        identifiers={("esphome", identifier)},
        manufacturer="Espressif Systems",
        model="Home Assistant Voice PE",
        name=name,
    )
    return esphome_entry, device


@pytest_asyncio.fixture
async def hass(tmp_path, monkeypatch):
    """A started, local-only Home Assistant instance (no network)."""
    hass = ha.HomeAssistant(str(tmp_path / "home"))
    loader.async_setup(hass)
    # The core "analytics" and "frontend" components have no bearing on this
    # integration but pull in real network services / a missing wheel, so keep
    # the test instance quiet and offline.
    import homeassistant.components.analytics as analytics
    import homeassistant.components.frontend as frontend

    async def _no_setup(_hass, _config):
        return True

    monkeypatch.setattr(analytics, "async_setup", _no_setup)
    monkeypatch.setattr(frontend, "async_setup", _no_setup)
    hass.config.skip_pip = True
    hass.config.skip_ip_resolv = True
    assert await async_from_config_dict({"homeassistant": {}}, hass)
    await hass.async_start()
    yield hass
    for entry in list(hass.config_entries.async_entries()):
        if entry.state is ConfigEntryState.LOADED:
            await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()
    await hass.async_stop()


@pytest.fixture
def esphome_device(hass):
    """Factory: register ESPHome devices bound to a fake esphome entry."""
    def _make(name="Voice PE", identifier="va-device-1", title=None):
        _, device = _make_esphome_device(hass, name, identifier, title)
        return device
    return _make


@pytest.fixture
def foreign_device(hass):
    """Factory: register a device owned by another integration."""
    def _make(name="MQTT Device", identifier="foreign-1", domain="mqtt"):
        owner_entry = _make_config_entry(
            hass,
            domain,
            {"host": "mock"},
            unique_id=identifier,
            title=name,
            disabled=True,
        )
        return dr.async_get(hass).async_get_or_create(
            config_entry_id=owner_entry.entry_id,
            identifiers={(domain, identifier)},
            manufacturer="Example",
            model="Example",
            name=name,
        )
    return _make


@pytest.fixture
def va_entry(hass):
    """Factory: create a voice_alarm config entry bound to a device id."""
    def _make(device_id, title="Voice PE"):
        return _make_config_entry(
            hass,
            DOMAIN,
            {CONF_DEVICE_ID: device_id},
            unique_id=device_id,
            title=title,
        )
    return _make
