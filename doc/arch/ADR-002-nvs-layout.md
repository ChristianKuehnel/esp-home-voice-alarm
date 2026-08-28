# ADR-002: NVS Data Layout & Storage Strategy

## Status
**Proposed**

## Context
The alarm clock stores up to 8 alarms in the ESP32's NVS (Non-Volatile Storage). NVS has a finite erase/write cycle limit (~100,000 cycles per block), but the ESPHome `preferences` component provides wear leveling that distributes writes across multiple NVS pages.

### NVS Endurance Analysis
- Flash endurance: ~100,000 erase cycles per block
- NVS wear leveling: distributes writes across multiple physical blocks
- Our write pattern: only on config changes (SetAlarm / DeleteAlarm / Snooze). No frequent writes.
- Example: 1 alarm set per day for 1 year = 365 writes. With wear leveling across 10 blocks = ~36 writes/block. **Negligible wear.**
- NVS partition size (ESPHome auto-generated): ~12-45 KB usable. We need < 1 KB for all alarm data.

## Decision
We use ESPHome's built-in `preferences` component (shared NVS partition) rather than creating a custom NVS partition. This keeps the ESPHome YAML config simple and avoids manual partition table management in v1.

**Layout:** A single binary blob stored in one ESPHome preference key.

### NVS Key Schema
```
"alarm_blob"  →  {uint8_t count; Alarm[0..MAX_ALARMS-1]}
```

### Alarm Struct (C++ — 4 bytes each)
```cpp
struct Alarm {
    uint8_t hour;       // 0-23
    uint8_t minute;     // 0-59
    uint8_t days_mask;  // Bitmask: bit 0=Mo ... bit 6=Su; 0 = one-shot
    bool enabled;       // Is this alarm active?
};
```

### Header + Blob Layout
```
[ count: uint8_t ][ Alarm[0] (4B) ][ Alarm[1] (4B) ] ... [ Alarm[7] (4B) ]
  (1 byte)           4 bytes              4 bytes                    4 bytes
```

**Total: 33 bytes** (1 byte header + 8 × 4 bytes = 33 bytes).
**~1 KB NVS space allocated** (default ESPHome NVS partition).
**~0.04% of NVS used.**

### Write Strategy
- **Full blob rewrite on change:** When adding/deleting/updating an alarm, the entire blob is written to the single key `"alarm_blob"`. This means 1 NVS write per alarm operation, not N writes per N keys.
- **Wear impact:** 1 alarm set/delete = 1 NVS write per key. With wear leveling, negligible.
- **Read on boot:** ESPHome loads preferences into RAM at startup. We deserialize the blob once into our C++ alarm manager.

## Alternatives Considered

| Alternative | Pros | Cons |
|-------------|------|------|
| **Single binary blob (chosen)** | ✅ 1 NVS write per operation, minimal code, fast I/O | Harder to debug, endianness concerns |
| Single JSON blob (one NVS key) | Human-readable, easy to extend | Needs `ArduinoJson` lib (Flash-heavy), slow serialization/deserialization, same single-write behavior |
| Flat key-value pairs | Max granularity | Many NVS keys (~24 per alarm), harder to manage, fragmented writes |
| C++ struct → binary in NVS (raw) | Fastest I/O | Endianness, no alignment guarantee, hard to debug |

## Consequences

### Positive
- Minimal NVS usage: 33 bytes vs ~45 KB available
- Minimal flash wear: 1 NVS write per alarm operation (not N writes per field)
- Simple ESPHome YAML config: just `preferences:` component, no custom partition
- Fast boot: single NVS read + 33 bytes deserialized into RAM

### Negative
- Harder to debug: NVS content is binary, not human-readable
- Must ensure struct layout is stable across firmware updates (no changing field order or types)
- No partial updates possible: changing one alarm requires rewriting the entire blob

## Open Questions
1. **Snooze storage:** **Resolved: Snooze removed from v1.** No snooze state in v1 — if alarm rings, user gets up. The `alarm_snoozed` event and Snooze state are removed from the state machine. If snooze is added in a future version, it would use a separate NVS key (`snooze_blob`) per the original design.
2. **Reminder text:** Resolved. Reminder text is stored in HA (as an `input_text` attribute or `text_sensor` state). ESP32 retrieves it via ESPHome API at trigger time. This follows the established ESPHome pattern (e.g., Voice PE — wake-word on ESP32, TTS payload from HA).
3. **Migration:** **Resolved: No automatic migration.** If `max_entries` is reduced while more entries exist in NVS, the config write fails with a clear error log. User must manually remove some alarms and reload. This matches the ESPHome pattern — ESPHome itself does not migrate NVS data on layout changes; it erases (`nvs_flash_erase`) or recommends `factory_reset`.
4. **Struct stability:** **Resolved: No version byte.** Layout changes use a new NVS key name (e.g., `"alarm_blob_v2"`). On boot, if the old key exists, a deprecation warning is logged and the blob is ignored. This follows the ESPHome convention — NVS is a config store, not a database, and migration is handled via factory reset or key renaming.

## References
- PRD D8 (NVS global limit of 8, configurable via YAML)
- PRD C1 (Snooze = separate NVS entry)
- PRD F4 (Generic C++ core)
- PRD NFR-02 (Data persistence across reboots)
- ESP-IDF NVS documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
- ESPHome `preferences` component: https://esphome.io/components/esp32/preferences.html