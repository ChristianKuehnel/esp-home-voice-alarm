"""Manifest and translation sanity checks (a lightweight hassfest stand-in).

hassfest only ships inside a source checkout of homeassistant/core (it has no
PyPI release), so these checks run in this suite instead: they validate the
fields hassfest would flag and that the HA loader requires.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

INTEGRATION = Path(__file__).resolve().parents[2] / "custom_components" / "voice_alarm"
VALID_IOT_CLASSES = {
    "assumed",
    "calculated",
    "cloud_poll",
    "cloud_push",
    "local_poll",
    "local_push",
}


def _load(name):
    with (INTEGRATION / name).open(encoding="utf-8") as handle:
        return json.load(handle)


def test_manifest_fields():
    manifest = _load("manifest.json")
    assert manifest["domain"] == INTEGRATION.name
    assert manifest["name"]
    assert re.fullmatch(r"\d+\.\d+\.\d+", manifest["version"])
    assert manifest["iot_class"] in VALID_IOT_CLASSES
    assert manifest["documentation"]
    assert manifest["issue_tracker"]
    assert manifest["codeowners"]
    assert isinstance(manifest.get("requirements", []), list)
    assert isinstance(manifest.get("dependencies", []), list)


def test_manifest_dependencies_are_built_in_integrations():
    import importlib.util

    for dependency in _load("manifest.json").get("dependencies", []):
        assert importlib.util.find_spec(f"homeassistant.components.{dependency}"), (
            f"dependency '{dependency}' is not a built-in HA integration"
        )


def test_strings_define_the_user_config_step():
    for path in ("strings.json", "translations/en.json", "translations/de.json"):
        steps = _load(path).get("config", {}).get("step", {})
        assert "user" in steps, f"{path} is missing config.step.user"
