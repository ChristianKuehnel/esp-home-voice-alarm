# esp-home-voice-alarm

> A voice-controlled alarm clock built with ESPHome and Home Assistant, bringing Alexa-style alarm functionality to your open-source smart home.

## The Idea

This project develops an integration for **ESPHome** and **Home Assistant** to create a voice-controlled alarm clock — a physical bedside device that replicates the core alarm experience of **Amazon Alexa**, but fully within your private, open-source smart home.

### Goals

- **Voice control**: Set, snooze, and cancel alarms using natural language via the Home Assistant Voice Assistant.
- **ESPHome hardware**: A dedicated ESP32-based device with display and audio output, running community-driven ESPHome firmware.
- **Home Assistant integration**: Seamless integration into HA automations, schedules, and the broader smart home ecosystem.
- **Alexa parity**: Achieve comparable alarm functionality — multi-alarm support, gradual wake-up tones, voice interaction, and smart home integrations (e.g., lights turning on with the alarm).

## Architecture

```
┌─────────────────────────────────────────────────┐
│              Home Assistant Core                 │
│  ┌──────────────┐    ┌───────────────────────┐  │
│  │ Voice        │    │ ESPHome Integration   │  │
│  │ Assistant    │◄──►│ (alarm entity node)   │  │
│  └──────────────┘    └──────────┬────────────┘  │
└─────────────────────────────────┼───────────────┘
                                  │ ESPHome / MQTT
                          ┌───────▼────────┐
                          │  ESP32 Device   │
                          │ (display,       │
                          │  speaker,       │
                          │  buttons)       │
                          └────────────────┘
```

## Status

**Early development** — this project starts as a standalone plugin. The goal is to prove the concept and eventually contribute the integration to the main ESPHome and/or Home Assistant codebases.

## Why Open Source?

Commercial voice assistants like Alexa are convenient but come with privacy trade-offs. This project aims to deliver the same alarm-clock convenience **without** sending your voice data to the cloud, running on hardware **you** own, with firmware **you** can audit and modify.

## Related Projects

- [ESPHome](https://esphome.io/) — Simple, declarative firmware for ESP32/ESP8266
- [Home Assistant](https://www.home-assistant.io/) — Open-source home automation platform
- [Home Assistant Voice Assistant](https://www.home-assistant.io/voice/) — Voice control for HA

## License

Licensed under **Apache License 2.0**, the same license as ESPHome.