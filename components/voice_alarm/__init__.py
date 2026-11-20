"""Portable scheduler wrapper; device-specific I/O lives in YAML."""

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time
from esphome.const import CONF_ID, CONF_ON_STATE, CONF_STATE, CONF_TIME_ID

AUTO_LOAD = ["time"]

voice_alarm_ns = cg.esphome_ns.namespace("voice_alarm")
VoiceAlarmClock = voice_alarm_ns.class_("VoiceAlarmClock", cg.PollingComponent)
AlarmState = voice_alarm_ns.enum("AlarmState")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VoiceAlarmClock),
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        # Fires on every scheduler state transition; `state` is the new state.
        cv.Optional(CONF_ON_STATE): automation.validate_automation({}),
    }
).extend(cv.polling_component_schema("1s"))

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(
        CONF_ON_STATE,
        "add_on_state_callback",
        args=[(AlarmState, CONF_STATE)],
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_TIME_ID in config:
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time_source(clock))
    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)
