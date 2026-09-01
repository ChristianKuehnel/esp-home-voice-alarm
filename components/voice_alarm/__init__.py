# Voice Alarm Clock — ESPHome External Component

# Validates YAML configuration and registers the VoiceAlarmClock component
# with the ESPHome code generation system.

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import esp32  # for pin validation

from esphome.const import (
    CONF_ID,
    CONF_PIN,
    CONF_SPEAKER_PIN,
    CONF_BUTTON_PIN,
    CONF_RTC_IRQ_PIN,
    DEVICE_CLASS_ALARM,
    ENTITY_CATEGORY_CONFIG,
)

# Auto-generate namespace
voice_alarm_ns = cg.esphome_ns.namespace("voice_alarm")
VoiceAlarmClock = voice_alarm_ns.class_(
    "VoiceAlarmClock", cg.Component
)

# Validation schema — all pins optional, ESPHome validates GPIO numbers
VOICE_ALARM_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VoiceAlarmClock),
        cv.Optional(CONF_SPEAKER_PIN): pins.GPIO_OUTPUT_PIN_SCHEMA,
        cv.Optional(CONF_BUTTON_PIN): pins.GPIO_INPUT_PULLUP_PIN_SCHEMA,
        cv.Optional(CONF_RTC_IRQ_PIN): pins.GPIO_INPUT_PIN_SCHEMA,
    }
)

CONFIG_SCHEMA = VOICE_ALARM_SCHEMA.extend(cv.COMPONENT)


async def to_code(config):
    """Generate C++ code from validated config."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    speaker = 0
    button = 0
    rtc = 0

    if config.get(CONF_SPEAKER_PIN):
        speaker = await cg.gpio_pin_expression(config[CONF_SPEAKER_PIN])
    if config.get(CONF_BUTTON_PIN):
        button = await cg.gpio_pin_expression(config[CONF_BUTTON_PIN])
    if config.get(CONF_RTC_IRQ_PIN):
        rtc = await cg.gpio_pin_expression(config[CONF_RTC_IRQ_PIN])

    if speaker or button or rtc:
        cg.add(var.set_gpio_config(speaker, button, rtc))