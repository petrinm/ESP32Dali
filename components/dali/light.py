import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_OUTPUT_ID,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from . import DALI_ns, DALIInterface

DEPENDENCIES = ["light"]

CONF_INTERFACE = "interface"
CONF_ADDRESS = "address"
CONF_GROUP = "group"
CONF_BROADCAST = "broadcast"
CONF_FADERATE = "faderate"
CONF_FADETIME = "fadetime"

DALILight = DALI_ns.class_("DALILight", light.LightOutput, cg.Component)

def _validate_ct_pair(value):
    has_cold = CONF_COLD_WHITE_COLOR_TEMPERATURE in value
    has_warm = CONF_WARM_WHITE_COLOR_TEMPERATURE in value
    if has_cold != has_warm:
        raise cv.Invalid(
            "cold_white_color_temperature and warm_white_color_temperature "
            "must be set together to enable colour temperature control",
        )
    return value


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(DALILight),
            cv.GenerateID(CONF_INTERFACE): cv.use_id(DALIInterface),
            cv.Optional(CONF_ADDRESS): cv.int_range(min=0, max=63),
            cv.Optional(CONF_GROUP): cv.int_range(min=0, max=15),
            cv.Optional(CONF_BROADCAST): cv.boolean,
            cv.Optional(CONF_FADERATE): cv.int_range(min=0, max=15),
            cv.Optional(CONF_FADETIME): cv.int_range(min=0, max=15),
            cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
            cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
        }
    ),
    cv.has_at_most_one_key(CONF_ADDRESS, CONF_GROUP, CONF_BROADCAST),
    light.validate_color_temperature_channels,
    _validate_ct_pair,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    await cg.register_component(var, config)

    interface = await cg.get_variable(config[CONF_INTERFACE])
    cg.add(var.set_interface(interface))

    if CONF_ADDRESS in config:
        cg.add(var.set_address(config[CONF_ADDRESS]))
    elif CONF_GROUP in config:
        cg.add(var.set_group(config[CONF_GROUP]))
    elif config.get(CONF_BROADCAST, False):
        cg.add(var.set_broadcast())

    if CONF_FADERATE in config:
        cg.add(var.set_fade_rate(config[CONF_FADERATE]))
    if CONF_FADETIME in config:
        cg.add(var.set_fade_time(config[CONF_FADETIME]))

    if CONF_COLD_WHITE_COLOR_TEMPERATURE in config:
        cg.add(var.set_min_mireds(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
        cg.add(var.set_max_mireds(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
        cg.add(var.set_color_temperature_enabled(True))
