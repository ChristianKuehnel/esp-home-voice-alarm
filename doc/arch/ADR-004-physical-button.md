# ADR-004: Physical Button

## Status
**Proposed**

## Context
PRD FR-10: "Physical snooze/stop button — P0 — Button detection and state-machine response"
Referenzen: [R-offline-05], [R-audio-01], [A 2.5.1]

Since snooze is removed from v1, the physical button has a single primary function: **stop a running alarm**. The hardware is device-specific and must be configured in ESPHome YAML.

## Decisions

### D1: Configurable GPIO pin (not hardcoded)

The button GPIO pin is defined as a YAML parameter in the `alarm_clock` component. This keeps firmware logic generic and lets device recipes (Voice PE, custom PCB, etc.) specify their own button.

```yaml
alarm_clock:
  stop_button:
    pin: GPIO4           # device-specific
    name: "Alarm Stop"
```

**Rationale:** Different hardware uses different physical buttons. The Voice PE uses the central knob button (see D2). A custom PCB might use a dedicated tactile switch. Hardcoding a pin would force C++ changes for every hardware variant — violates D7 (Generic C++ core + YAML-only hardware config).

### D2: Default for Voice PE — central knob button

On the ESP32-S3 Voice PE, the physical button at the center of the encoder knob is mapped to `GPIO_1` (or the equivalent pin defined by the `voice_pe` ESPHome integration). Users configure it in their YAML:

```yaml
alarm_clock:
  stop_button:
    pin: ${voice_pe_knob_button_pin}   # typically GPIO1 for Voice PE
    name: "Alarm Stop"
```

**Rationale:** The Voice PE already has a physical button — no additional hardware needed for v1. This is the expected default for Voice PE users.

### D3: Button behavior by state

| State | Short Press | Long Press (3s) |
|-------|-------------|-----------------|
| **IDLE** | No action (leave Voice PE behavior untouched) | No action |
| **SET** (alarm being configured) | Confirm → fire `alarm_set`, transition to `IDLE` | Cancel → reset input, transition to `IDLE` |
| **FIRING** (alarm ringing) | Stop alarm → transition to `IDLE` | No action |
| **FIRING** (reminder TTS playing) | Stop TTS → transition to `IDLE` | No action |

**Rationale:** The button is always offline and always works — even if Wi-Fi is down, HA is down, or the Voice Assistant pipeline is broken. This is the core value of a physical button.

### D4: Debounce via ESPHome `binary_sensor` config

Debounce is handled by ESPHome's built-in `binary_sensor` noise_filter / debounce_filter. No custom C++ debounce logic needed. Default: 50ms.

```yaml
binary_sensor:
  - platform: gpio
    pin: ${stop_button_pin}
    name: "Alarm Stop"
    filters:
      - delayed_on_off: 50ms   # debounce
    on_press:
      then:
        - lambda: |-
            // State machine transition logic
```

**Rationale:** ESPHome's debounce filter is proven and tested. Adding custom C++ debounce logic would be redundant and error-prone.

## Open Questions
1. **LED feedback on button press during FIRING?** Should the LED flash briefly (100ms) to confirm the stop was registered? Or is audio stop (silence) sufficient feedback?
2. **Long-press in SET state** — Is cancel the right behavior? Or should long-press be unassigned in v1 to avoid confusion?