"""Portable scheduler wrapper; device-specific I/O lives in YAML."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time
from esphome.const import CONF_ID, CONF_TIME_ID

AUTO_LOAD = ["time"]

voice_alarm_ns = cg.esphome_ns.namespace("voice_alarm")
VoiceAlarmClock = voice_alarm_ns.class_("VoiceAlarmClock", cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VoiceAlarmClock),
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
    }
).extend(cv.polling_component_schema("1s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_TIME_ID in config:
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time_source(clock))
