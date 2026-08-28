# ADR-002: NVS Data Layout & Serialization

## Status
**Proposed**

## Context
The alarm clock stores alarms and reminders in the ESP32's NVS (Non-Volatile Storage). The PRD defines a **global limit of 8 entries** (alarms + reminders combined), configurable via YAML `max_entries: X`.

This creates several constraints:
1. **Flash wear:** NVS has a finite erase/write cycle limit. We must minimize unnecessary writes.
2. **Memory efficiency:** ESP32 has limited RAM. We cannot afford bloated data structures.
3. **Extensibility:** Future versions may add new fields (custom sounds, fade-in, display config) — the layout must accommodate this without breaking existing entries.
4. **Atomicity:** NVS writes are atomic per-key, but not across keys. A power loss mid-write must not corrupt existing entries.

## Decision
We use a **hybrid approach**: a compact C++ struct for each entry, serialized to JSON for HA-level transport, but stored in NVS as individual key-value pairs.

### NVS Key Schema
```
alarm_clock_config (global metadata)
alarm_clock_entry_0  (alarm/reminder entry 0)
alarm_clock_entry_1  (alarm/reminder entry 1)
...
alarm_clock_entry_7  (alarm/reminder entry 7)
```

### Entry Struct (C++)
```cpp
struct AlarmEntry {
    uint8_t id;              // 0-7 (index into NVS)
    bool active;             // Is this alarm/reminder enabled?
    uint8_t type;            // 0 = alarm, 1 = reminder
    uint8_t hour;            // 0-23
    uint8_t minute;          // 0-59
    uint8_t repeat_mask;     // Bitmask: bit 0 = Sunday, bit 6 = Saturday
    uint8_t sound_id;        // 0 = default (buzzer), 1-3 = reserved for v2 sounds
    uint16_t snooze_minutes; // Snooze duration in minutes
    char reminder_text[64];  // Max 64 bytes for reminder text
    char tag[8];             // "alarm" or "reminder" for quick lookup
};
```

### JSON Payload (HA ↔ ESP32 transport)
```json
{
    "id": 0,
    "active": true,
    "type": "alarm",
    "hour": 8,
    "minute": 0,
    "repeat_mask": 0b01111110,
    "sound_id": 0,
    "snooze_minutes": 5,
    "reminder_text": ""
}
```

### Write Strategy
- **Read-modify-write per entry:** When updating entry N, read entry N from NVS, modify in RAM, write back to NVS key `alarm_clock_entry_N`. Never rewrite the entire namespace.
- **No entry is ever deleted:** Setting `active = false` is preferred over erasing the key. This avoids fragmentation and preserves the sequential key pattern.
- **Config changes (max_entries):** Written once to `alarm_clock_config`. If this value conflicts with existing entries (e.g., new max_entries is 4 but entries 5-7 exist), the config write fails with an error.

## Alternatives Considered

| Alternative | Pros | Cons |
|-------------|------|------|
| Single JSON blob (one NVS key) | Simple read/write, easy to extend | Must rewrite entire NVS key on every update (more flash wear), harder to make atomic |
| Flat key-value pairs (`alarm_0_hour`, `alarm_0_active`, ...) | Max granularity, no serialization | Many NVS keys, harder to manage, no native struct alignment |
| C++ struct → binary in NVS | Fastest I/O, smallest footprint | Endianness issues, no human readability, harder to debug |
| **Hybrid (struct + JSON transport)** | ✅ Best of both worlds | Slightly more code, but worth it |

## Consequences

### Positive
- Minimal flash wear: only the changed entry is rewritten
- Backward compatible: adding new struct fields (v2/fade-in, etc.) is safe as long as existing fields are at fixed offsets
- JSON payload for HA transport is human-readable and easily parsed by LLMs (per D10)
- Entry deletion via `active = false` prevents NVS fragmentation

### Negative
- 8 entries × ~100 bytes struct ≈ 800 bytes + overhead ≈ ~2-3 KB NVS space (still well within ESP32's ~45 KB usable NVS)
- JSON serialization/deserialization on the ESP32 adds ~5 KB code size (esp-json/tinyjson)
- `active = false` entries accumulate until manual cleanup — need a periodic compaction mechanism (v2+)

## Open Questions
1. **Entry count:** 8 is the default, but should we support a "soft limit" (e.g., 16 entries) where overflow entries are queued in a FIFO and auto-pruned (oldest first) when max_entries is reached? This would be v2.
2. **Snooze storage:** Should snoozed alarms be stored as a *separate* entry (new NVS key `alarm_snooze_0`) as defined in C1, or as a temporary state in RAM that auto-deletes when it fires? (Answer per PRD: separate NVS entry.)
3. **Migration:** What happens if the YAML `max_entries` is changed from 8 to 4 while 6 entries exist? Reject the config change, or auto-truncate oldest entries?
4. **Reminder text encoding:** 64 bytes for `reminder_text` — is that enough? Voice-generated TTS text can be verbose.

## References
- PRD D8 (NVS global limit, configurable via YAML)
- PRD C1 (Snooze = new NVS entry)
- PRD F4 (Generic C++ core)
- PRD NFR-02 (Data persistence across reboots)
- esp-idf NVS documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html