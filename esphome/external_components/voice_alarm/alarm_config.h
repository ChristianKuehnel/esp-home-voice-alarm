#pragma once

#ifdef USE_ESP32

namespace esphome {
namespace voice_alarm {

// Default GPIO pin definitions per device

// Home Assistant Voice PE (ESP32-S3)
// Mic: I2S via XMOS XU316
// Speaker: Buzzer on GPIO
// Button: Knob button on GPIO1
struct VoicePEConfig {
  static const uint8_t speaker_pin = GPIO_NUM_25;
  static const uint8_t button_pin = GPIO_NUM_1;
  static const uint8_t rtc_irq_pin = GPIO_NUM_NC;  // Not used on Voice PE
};

// FutureProofHomes Satellite1 (ESP32-S3)
// Mic: I2S via XMOS XU316
// Speaker: 25W amplifier
// Button: Physical button
// mmWave: LD2450
struct Satellite1Config {
  static const uint8_t speaker_pin = GPIO_NUM_26;
  static const uint8_t button_pin = GPIO_NUM_2;
  static const uint8_t rtc_irq_pin = GPIO_NUM_15;
};

// Generic fallback (for testing/prototyping)
struct GenericConfig {
  static const uint8_t speaker_pin = GPIO_NUM_25;
  static const uint8_t button_pin = GPIO_NUM_0;  // Boot button, active low
  static const uint8_t rtc_irq_pin = GPIO_NUM_NC;
};

}  // namespace voice_alarm
}  // namespace esphome

#endif