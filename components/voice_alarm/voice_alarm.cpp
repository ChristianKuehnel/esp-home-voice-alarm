#include "voice_alarm.h"

#ifdef USE_ESP32

#include <esp_timer.h>

namespace esphome {
namespace voice_alarm {

void VoiceAlarmClock::setup() {
  ESP_LOGI("voice_alarm", "Voice Alarm Clock component initializing...");

  if (state_machine_ != nullptr) {
    state_machine_->init();
    ESP_LOGI("voice_alarm", "State machine initialized");
  } else {
    ESP_LOGW("voice_alarm", "No state machine connected — alarm functionality limited");
  }

  if (speaker_pin_ != 0) {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << speaker_pin_);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    ESP_LOGI("voice_alarm", "Speaker pin GPIO%d configured", speaker_pin_);
  }

  if (button_pin_ != 0) {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << button_pin_);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io_conf);
    ESP_LOGI("voice_alarm", "Button pin GPIO%d configured (active low)", button_pin_);
  }

  if (rtc_irq_pin_ != 0) {
    ESP_LOGI("voice_alarm", "RTC IRQ pin GPIO%d configured", rtc_irq_pin_);
  }

  ESP_LOGI("voice_alarm", "Voice Alarm Clock component ready");
}

void VoiceAlarmClock::loop() {
  if (state_machine_ != nullptr) {
    state_machine_->tick();
  }
}

void VoiceAlarmClock::dump_config() {
  ESP_LOGCONFIG(TAG, "Voice Alarm Clock:");
  LOG_PIN("  Speaker Pin: ", speaker_pin_);
  LOG_PIN("  Button Pin: ", button_pin_);
  if (rtc_irq_pin_ != 0) {
    LOG_PIN("  RTC IRQ Pin: ", rtc_irq_pin_);
  }
  ESP_LOGCONFIG(TAG, "  Has RTC: %s", YESNO(has_rtc_));
  ESP_LOGCONFIG(TAG, "  Last NTP sync: %lu seconds ago",
                (uint32_t)(millis() / 1000 - last_sync_epoch_));
}

}  // namespace voice_alarm
}  // namespace esphome

#endif