# esp-home-voice-alarm

> A voice-controlled alarm clock built with ESPHome and Home Assistant, bringing Alexa-style alarm functionality to your open-source smart home.

## The Idea

This project develops an integration for **ESPHome** and **Home Assistant** to create a voice-controlled alarm clock — a physical bedside device that replicates the core alarm experience of **Amazon Alexa**, but fully within your private, open-source smart home.

### Goals

- **Voice control**: Set and cancel alarms in German and English via Home Assistant Assist. Dismiss ringing locally with the wake word or physical button.
- **ESPHome hardware**: Home Assistant Voice PE first, using its existing speaker, button, and local wake-word facilities.
- **Home Assistant integration**: Seamless integration into HA automations, schedules, and the broader smart home ecosystem.
- **Reliable alarms**: Persistent one-time and repeating schedules, independent volume, and local ringing during network outages. Reminders, snooze, and gradual wake-up tones follow later.

## Architecture

```
┌─────────────────────────────────────────────────┐
│              Home Assistant Core                 │
│  ┌──────────────┐    ┌───────────────────────┐  │
│  │ Voice        │    │ ESPHome Integration   │  │
│  │ Assistant    │◄──►│ + voice_alarm         │  │
│  └──────────────┘    └──────────┬────────────┘  │
└─────────────────────────────────┼───────────────┘
                                  │ ESPHome native API
                          ┌───────▼────────┐
                          │  ESP32 Device   │
                          │ (schedules,     │
                          │  speaker,       │
                          │  buttons)       │
                          └────────────────┘
```

## Status

**Early development** — this project starts as a standalone plugin. The goal is to prove the concept and eventually contribute the integration to the main ESPHome and/or Home Assistant codebases.

The owner interview established the [v1 implementation baseline](doc/arch/ADR-009-v1-implementation-baseline.md). Code remains scaffolding; follow the [implementation plan and acceptance cases](doc/implementation-plan.md) to build the first release.

The v1 target is for already-set alarms to work offline while the device stays powered with valid time. After reboot, schedules will wait for time synchronization. Occurrences missed by at most five minutes will ring; older ones will be skipped. Alarms will share a ringing session and stop after 15 minutes or immediate button/wake-word dismissal.

## Testing

The [test guide](test/README.md) covers portable C++ scheduler checks, compiled ESPHome host tests through its native API, and compiled ESP32-S3 firmware running in Espressif's `esp-emu`. CI runs all three layers, including preference persistence across restarts. Voice PE audio, microphone, and physical controls still require hardware acceptance tests.

## Why Open Source?

Commercial voice assistants like Alexa are convenient but come with privacy trade-offs. This project aims to deliver the same alarm-clock convenience **without** sending your voice data to the cloud, running on hardware **you** own, with firmware **you** can audit and modify.

## Related Projects

- [ESPHome](https://esphome.io/) — Simple, declarative firmware for ESP32/ESP8266
- [Home Assistant](https://www.home-assistant.io/) — Open-source home automation platform
- [Home Assistant Voice Assistant](https://www.home-assistant.io/voice/) — Voice control for HA

## License

Licensed under **Apache License 2.0**, the same license as ESPHome.
