# ADR-003: Intent Payload & API Schema

## Status
**Proposed**

## Context
The alarm clock exposes **three input paths** (PRD D10), all converging on the same alarm state machine:

1. **HA Voice Assistant** — Wake Word → transcription → intent (`SetAlarm`, `CancelAlarm`, etc.)
2. **HA Integrated LLM** — Natural language → parameter extraction → same intent
3. **HA via MCP** — External apps call MCP tools → same underlying ESPHome service

The PRD defines `alarm_clock.set_alarm` (HA service call, FR-26) and `alarm_clock.fire` (ESPHome event, FR-27) as the core communication mechanism.

We need to define:
- The **JSON schema** for intent payloads exchanged between HA and the ESP32
- The **ESPHome service call** signature
- The **ESPHome event** payload structure
- How the three input paths map to a single, canonical internal representation

## Decision

### Canonical Intent Payload
All three input paths must produce the same canonical JSON structure before calling the ESPHome service. This is the single source of truth for alarm/reminder parameters.

```json
{
    "id": 0,
    "type": "alarm",
    "active": true,
    "hour": 8,
    "minute": 0,
    "repeat_mask": 62,
    "sound_id": 0,
    "snooze_minutes": 5,
    "reminder_text": ""
}
```

**Field definitions:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `id` | int | No | Auto-assigned (next free) | Entry index (0-7). If omitted, HA picks the first free slot. |
| `type` | string | Yes | `"alarm"` | `"alarm"` or `"reminder"` |
| `active` | bool | Yes | `true` | Whether the entry is enabled |
| `hour` | int | Yes | — | 0-23 |
| `minute` | int | Yes | — | 0-59 |
| `repeat_mask` | int | No | `0b00000000` | Bitmask: bit 0 = Sunday, ..., bit 6 = Saturday. `62` = Mon-Sat. `126` = every day (Sun-Sat). |
| `sound_id` | int | No | `0` | `0` = default buzzer. `1-3` reserved for v2 custom sounds. |
| `snooze_minutes` | int | No | `5` | Duration for snooze (C1). |
| `reminder_text` | string | No | `""` | Text for TTS playback on reminder fire. |

### ESPHome Service Call (FR-26)
```yaml
# alarm_clock.set_alarm
service: alarm_clock.set_alarm
data:
  payload:
    type: alarm
    hour: 8
    minute: 0
    repeat_mask: 62
```

### ESPHome Event (FR-27)
```yaml
# alarm_clock.fire
trigger:
  - platform: event
    event_type: alarm_clock.fire
    event_data:
      id: 0
      type: alarm
      hour: 8
      minute: 0
```

### Input Path Mapping

| Input Path | How it produces canonical payload | Example |
|------------|----------------------------------|---------|
| **Voice Assistant** | Intent defined in HA `intent.json` → HA framework extracts slots → passes to service call | "Weck mich um 8 Uhr werktags" → `SetAlarm(hour=8, minute=0, repeat_mask=62)` |
| **LLM** | LLM receives intent definition + parameter schema → extracts natural language into slots → passes to service call | "Sag mir in 20 Minuten Bescheid" → LLM calculates `hour=now+20min` → `reminder` type → service call |
| **MCP** | MCP server (HA add-on or external) exposes `set_alarm`, `cancel_alarm`, `list_alarms`, `get_alarm_state` as native tools → external app constructs JSON → calls HA service | Python script: `client.set_alarm(type="alarm", hour=6, minute=30)` → constructs JSON → calls service |

## Alternatives Considered

| Alternative | Pros | Cons |
|-------------|------|------|
| **Canonical JSON (above)** | ✅ Single schema, LLM-friendly, type-safe, extensible | More bytes on wire (acceptable) |
| ESPHome native service parameters (typed fields) | No JSON serialization, faster | No JSON for LLM parsing, no MCP tool compatibility, fragile type changes |
| Free-form string (e.g., `"8 Uhr werktags"`) | Simplest for LLM, no schema | ESP32 must parse it, no type safety, no automation compatibility |
| YAML config file (HA writes to ESPHome via file transfer) | Human-editable | Heavyweight, not suitable for frequent updates |

## Consequences

### Positive
- **Single schema** means all three input paths (voice, LLM, MCP) use the same structure — no duplication
- **JSON is LLM-friendly:** The LLM pipeline (D10 path #2) can naturally produce this structure from natural language
- **Type-safe:** ESPHome YAML schema can validate the payload before sending to ESP32
- **Extensible:** Adding `fade_in`, `custom_sound`, `display_text` (v2/v3) is just adding new fields — old entries keep their defaults

### Negative
- JSON serialization on ESP32 adds ~5 KB code size (tinyjson)
- `repeat_mask` uses a bitmask — the LLM or intent system must understand how to convert "werktags"/"every day" into the right integer

### repeat_mask mapping:
```
Bit 0 = Sunday   → value 1
Bit 1 = Monday   → value 2
Bit 2 = Tuesday  → value 4
Bit 3 = Wednesday→ value 8
Bit 4 = Thursday → value 16
Bit 5 = Friday   → value 32
Bit 6 = Saturday → value 64

"werktags" (Mon-Fri)  = 2+4+8+16+32 = 62
"jeden Tag" (all days) = 1+2+4+8+16+32+64 = 127
"MoMiFr" (Tue/Thu)    = 4+16 = 20
"Samstag"             = 64
```

## Open Questions
1. **MCP server location:** Should the MCP tools be implemented as a **HA add-on**, or as a **HA integration** that exposes the service calls as MCP tools? An add-on is more portable (works with any HA deployment), but an integration is more tightly coupled (can directly call ESPHome services without HA API overhead).
2. **Intent definitions:** Should the `SetAlarm`/`CancelAlarm` intents be defined as custom HA intents, or should we leverage the existing HA timer intents if any? (Answer: custom intents — no existing timer intents.)
3. **Cancel semantics:** Should `cancel_alarm` take an `id` (specific alarm) or `all` (cancel all)? The MCP `cancel_alarm` tool should support both via an optional `target` parameter.
4. **Timezone handling in payloads:** The canonical payload uses absolute `hour`/`minute` in local time (as configured in D6). Should we also store the timezone in the payload, or is it always inherited from the device config? (Answer: inherited from device config — the payload just stores the time.)

## References
- PRD D10 (Three input paths)
- PRD FR-26 (Set alarm from HA automations)
- PRD FR-27 (Alarm-triggered automations)
- PRD D9 (24h/12h format)
- PRD F2 (Repeat days, bitmask)