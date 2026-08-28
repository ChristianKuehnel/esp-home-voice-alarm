# ADR-005: Alarm Volume Control

## Status
**Proposed**

## Context
PRD FR-09: "Alarm volume control — P0 — Independent of media volume. Ascending volume (fade-in) is v2."
Referenzen: [A 2.4.1], [R-audio-04]

The alarm volume must be settable and adjustable by the user. The alarm volume is absolute (independent of media volume) and uses the same 0.0–1.0 float scale as the Voice PE standard volume. No fade-in (ascending volume) in v1.

## Decisions

### D1: HA Number Entity for runtime control (Option B)

The alarm_clock component exposes a `number` entity in HA:

```yaml
# Entity: number.alarm_clock_alarm_volume
# Range: 0.1 to 1.0 (float, min 0.1 to prevent accidental mute)
# Step: 0.01
# Default: 0.7 (configurable via YAML `alarm_volume: 0.7`)
```

This allows runtime adjustment from HA UI, voice assistant ("set alarm volume to 50%"), or automations. The value is persisted in NVS (stored alongside alarm entries, consuming one NVS slot — see ADR-002).

**Rationale:** The user explicitly wants runtime control from the start. A YAML-only approach (Option A) forces the user to edit YAML and flash for every volume change. Since the alarm_clock already communicates via ESPHome Native API, adding a `number` entity is trivial and uses existing infrastructure. This matches the HA/ESPHome idiomatic pattern (see NFR-09). Min 0.1 prevents accidental mute.

### D2: Volume range — 0.0 to 1.0 (float)

The volume scale uses the same range as the Voice PE's standard volume (0.0 = mute, 1.0 = max). This ensures a consistent user experience when switching between alarm and voice assistant output.

**Rationale:** Voice PE already uses this scale for its media/volume control. Aligning avoids confusion.

### D3: Absolute volume (independent of media volume)

The alarm volume is an absolute value applied directly to the I2S output. It does NOT scale with media volume (music, TV, etc.). If media is at 30% and alarm is set to 80%, the alarm plays at 80% of max volume.

**Rationale:** A loud alarm is the correct UX — the goal is to wake the user. Relative volume would create an unpredictable experience (e.g., media at 5% → alarm at 4% → not loud enough to wake up).

### D4: No fade-in (ascending volume)

Fade-in (ascending volume) is deferred entirely to v2. No infrastructure is built in v1 to support it. When fade-in is added later, the alarm_volume value will be the **final** volume after fade-in completes.

**Rationale:** The PRD explicitly states fade-in is v2. Adding fade-in infrastructure now would increase firmware complexity and NVS requirements (fade-in state, duration) without adding value in v1.

### D5: NVS persistence

The `alarm_volume` value is stored in NVS as a `float` preference. It survives reboots and is loaded on startup. This consumes one NVS slot toward the global 8-entry limit (see ADR-002).

```yaml
# NVS key: "alarm_volume_pref" (stored via esphome::preferences)
# Type: float (4 bytes)
# Default: 0.7
# Range: 0.1 - 1.0
```

## Open Questions

_No remaining questions. All resolved._