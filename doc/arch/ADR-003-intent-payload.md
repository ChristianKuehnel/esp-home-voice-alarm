# ADR-003: Intent Payload & MCP Integration

## Status
**Proposed**

## Context
The alarm clock exposes **three input paths** (PRD D10), all converging on the same alarm state machine:

1. **HA Voice Assistant** — Wake Word → transcription → intent (`SetAlarm`, `CancelAlarm`, etc.)
2. **HA Integrated LLM** — Natural language → parameter extraction → same intent
3. **External Agent via MCP** — Agent calls MCP tools → HA service → ESP32

The PRD defines `alarm_clock.set_alarm` (HA service call, FR-26) and `alarm_clock.fire` (ESPHome event, FR-27) as the core communication mechanism.

**Critical constraint:** An external agent (e.g., LLM agent, automation system) must be able to create and delete alarms via the **HA MCP Server**. This requires:
- A proper **ESPHome Integration** in HA Core that exposes alarm services
- **MCP Tools** that wrap these services for the agent to call

## Decision

### 1. Architecture: ESPHome Component + HA Core Platform

The alarm clock follows the **ESPHome native integration pattern** used by all other ESPHome devices:

```
External Agent (optional v2)
       │
       │ MCP Protocol (HTTP/WebSocket)
       ▼
┌─────────────────────┐
│ HA MCP Server        │  ← Optional add-on (v2+)
│  (built-in HA        │     Not required for v1
│   add-on)            │
└──────────┬───────────┘
           │ HA Service Call
           ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Home Assistant Core                                                 │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ Platform: alarm_clock (core or config_entries, auto-detected│   │
│  │ from ESPHome Device Registry. No HACS component needed.    │   │
│  │ - Reads entities from ESP32 device                         │   │
│  │ - Provides HA Services: alarm_clock.set, cancel, list      │   │
│  │ - Manages reminder_text storage (HA Storage API)           │   │
│  └─────────────────────────────────────────────────────────────┘   │
└──────────┬──────────────────────────────────────────────────────────┘
           │ ESPHome API (WebSocket)
           ▼
┌─────────────────────────────────────────────────────────────────────┐
│ ESPHome Component (C++) on ESP32                                    │
│  ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐    │
│  │ binary_sensor    │ │ text_sensor      │ │ number           │    │
│  │ alarm_firing     │ │ alarm_state      │ │ alarm_volume     │    │
│  │ (ON=alarm firing)│ │ (IDLE/FIRING/..) │ │ (0.0-1.0)        │    │
│  └──────────────────┘ └──────────────────┘ └──────────────────┘    │
│                                                                     │
│  NVS Storage + RTC Tick + Alarm State Machine                     │
│  Custom Action: alarm.stop (called from voice_assistant.on_intent) │
│                                                                     │
│  Intent Handling:                                                   │
│  - voice_assistant.on_intent: Stop intent → alarm.stop action     │
│  - ESPHome API: HA services write to alarm state machine          │
│  - alarm_clock.fire event: HA automation trigger                  │
│                                                                     │
│  Reminder Text Retrieval:                                         │
│  - ESPHome API call to HA at trigger time (fetches reminder_text) │
│  - No reminder_text stored in ESP32 NVS                           │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ Custom Action: alarm.stop                                  │   │
│  │ - Defined in C++ component via ESPHome action framework    │   │
│  │ - Stops alarm ringing, resets state to IDLE                │   │
│  │ - Called from: voice_assistant.on_intent Stop              │   │
│  │                  ESPHome binary_sensor.on_press (D2)       │   │
│  │                  HA service alarm_clock.cancel             │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

**Key decisions:**
- The ESP32 communicates with HA via **ESPHome API** (WebSocket, already built into ESPHome).
- All alarm entities (BinarySensor, TextSensor, Number) are defined **in the ESPHome C++ component**, following the standard ESPHome device pattern. HA's Core Platform auto-discovers them from the Device Registry.
- HA provides **native services** (`alarm_clock.set`, `alarm_clock.cancel`, `alarm_clock.list`) that write to the alarm state machine on the ESP32.
- The **MCP Server** wraps these services as MCP tools for external agents — but is **OPTIONAL for v1**. Core HA services are sufficient.
- Reminder text is stored in HA (Entity Attribute + HA Storage persistence) and fetched by ESP32 via ESPHome API at trigger time.
- A **Custom Action** `alarm.stop` is registered in the ESPHome C++ component for use from `voice_assistant.on_intent` and button press handlers.
- No custom protocol needed — uses the existing ESPHome API + HA service layer.

### 2. Canonical Intent Payload
All three input paths produce the same JSON structure:

```json
{
    "id": 0,
    "type": "alarm",
    "active": true,
    "hour": 8,
    "minute": 0,
    "repeat_mask": 62,
    "reminder_text": ""
}
```

**Field definitions:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `id` | int | No | Auto-assigned (next free) | Entry index (0-7). If omitted, HA picks the first free slot. |
| `type` | string | Yes | `alarm` | `alarm` or `reminder` |
| `active` | bool | Yes | `true` | Whether the entry is enabled |
| `hour` | int | Yes | — | 0-23 |
| `minute` | int | Yes | — | 0-59 |
| `repeat_mask` | int | No | `0` | Bitmask: bit 0=Sun, bit 1=Mon, ..., bit 6=Sat. `0` = one-shot. `62` = Mon-Fri. `127` = every day. |
| `reminder_text` | string | No | `""` | TTS text for reminders. Stored in HA (not sent to ESP32). |

**Note:** No `snooze_minutes` or `sound_id` — snooze is removed from v1 (ADR-001). `reminder_text` is stored in HA, not in the NVS blob on ESP32.

### 3. HA Services

```yaml
# alarm_clock.set
service: alarm_clock.set
data:
  type: alarm
  hour: 8
  minute: 0
  repeat_mask: 62
  # Optional: id, reminder_text (stored in HA only)

# alarm_clock.cancel
service: alarm_clock.cancel
data:
  # Optional: id
  # If id is provided → cancel specific alarm
  # If id is omitted → cancel all alarms
id: 0  # optional

# alarm_clock.list
service: alarm_clock.list
# Returns: list of all alarms (from ESP32 NVS)

# alarm_clock.trigger_tts (optional v2)
service: alarm_clock.trigger_tts
data:
  id: 0
  text: "Meeting reminder"
# Triggers TTS playback on ESP32 (used for reminders)
```

### 4. MCP Tools (for External Agent)

The MCP Server exposes the following tools to the agent:

| MCP Tool | Description | Parameters |
|----------|-------------|------------|
| `set_alarm` | Create/update an alarm | `type`, `hour`, `minute`, `repeat_mask` (optional: `id`, `reminder_text`) |
| `cancel_alarm` | Cancel an alarm | `id` (optional, omit = cancel all) |
| `list_alarms` | List all alarms | none |

**Example agent call:**
```
Agent: "Set alarm for 7 AM weekdays"
→ MCP call: `set_alarm(type="alarm", hour=7, minute=0, repeat_mask=62)`
→ HA service: `alarm_clock.set(hour=7, minute=0, repeat_mask=62)`
→ ESPHome API: write to ESP32 NVS
→ ESP32: alarm stored, ready to fire
```

### 5. Input Path Mapping

| Input Path | How it produces canonical payload | Example |
|------------|----------------------------------|---------|
| **Voice Assistant** | Intent defined in HA `intent.json` → HA framework extracts slots → passes to service call | "Weck mich um 8 Uhr werktags" → `SetAlarm(hour=8, minute=0, repeat_mask=62)` |
| **LLM** | LLM receives intent definition + parameter schema → extracts natural language into slots → passes to service call | "Sag mir in 20 Minuten Bescheid" → LLM calculates `hour=now+20min` → `reminder` type → service call |
| **MCP Agent** | Agent calls MCP tool → MCP Server calls HA service → ESPHome API to ESP32 | Agent: "Set alarm for 7 AM weekdays" → `set_alarm(type="alarm", hour=7, minute=0, repeat_mask=62)` |

### 6. `repeat_mask` Bitmask Mapping

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

## Alternatives Considered

| Alternative | Pros | Cons |
|-------------|------|------|
| **HA Integration + MCP Server (chosen)** | ✅ Agent can call alarm via MCP, clean separation of concerns, uses existing ESPHome API | Requires custom HA integration (mild effort) |
| MCP Server directly calls ESPHome API | No HA integration needed | Breaks HA abstraction layer, agent bypasses HA UI/state |
| Native ESPHome service parameters | No JSON serialization, faster | Not MCP-compatible, fragile type changes |
| Free-form string (e.g., `"8 Uhr werktags"`) | Simplest for LLM | ESP32 must parse, no type safety |

## Consequences

### Positive
- **Agent-first design:** External agents (LLM, automation) can directly manage alarms via MCP — no manual HA UI interaction needed.
- **Single schema:** All three input paths use the same structure — voice, LLM, MCP all produce identical JSON.
- **Extensible:** Adding fields (fade-in, custom TTS prompts) is backward-compatible (new fields have defaults).
- **HA-native:** Uses ESPHome API and HA service layer — no custom networking or protocol.

### Negative
- JSON serialization on ESP32 adds ~5 KB code size (tinyjson).
- `repeat_mask` bitmask requires the LLM/MCP agent to understand how to convert "werktags"/"every day" into the right integer (can be handled via MCP tool documentation).

## Open Questions

1. **ESPHome integration vs custom component:** **Resolved: ESPHome C++ component (Core pattern).** Entities (BinarySensor, TextSensor, Number) are defined in the ESPHome C++ component on the ESP32. HA's Core Platform auto-discovers them from the ESPHome Device Registry — no HACS component needed. HA provides services (`alarm_clock.set`, `cancel`, `list`) that communicate with the ESP32 via ESPHome API. This is the standard pattern for all ESPHome devices (e.g., `voice_assistant`, `media_player`).

2. **Reminder text storage:** **Resolved:** Entity Attribute + HA Storage persistence. The HA Platform stores reminder text in an entity attribute and persists to `storage/` on every change. ESP32 fetches reminder text via ESPHome API at trigger time.

3. **`cancel_alarm` semantics:** **Resolved:** Option A (Cancel all). `alarm_clock.cancel()` without `id` removes ALL alarms. `alarm_clock.cancel(id=0)` removes only entry 0.

4. **ESP32 entity state:** **Resolved:** Both. `text_sensor.alarm_clock_state` for detailed mode (`IDLE`, `SET`, `FIRING`, `FIRING_ALARM`, etc.) and `binary_sensor.alarm_clock_firing` for simple on/off automation logic. Both exposed as ESPHome entities in the C++ component.

5. **Custom Action `alarm.stop`:** **Resolved:** Implemented in ESPHome C++ component via `ESPHomeAction` framework. Registered as `alarm.stop` and callable from `voice_assistant.on_intent`, `binary_sensor.on_press`, and HA service calls. The action resets the alarm state machine to `IDLE` and stops audio output.

## References
- PRD D10 (Three input paths)
- PRD FR-26 (Set alarm from HA automations)
- PRD FR-27 (Alarm-triggered automations)
- PRD D9 (24h/12h format)
- PRD F2 (Repeat days, bitmask)
- HA MCP Server documentation: https://home-assistant.io/integrations/mcp/
- ESPHome API documentation: https://esphome.io/guides/connections.html