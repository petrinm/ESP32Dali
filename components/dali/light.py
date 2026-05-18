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
CONF_COLD_WHITE_ADDRESS = "cold_white_address"
CONF_WARM_WHITE_ADDRESS = "warm_white_address"

CtMode = DALI_ns.enum("CtMode")

DALILight = DALI_ns.class_("DALILight", light.LightOutput, cg.Component)

_SINGLE_ADDRESS_KEYS = [CONF_ADDRESS, CONF_GROUP, CONF_BROADCAST]
_CT_TEMP_KEYS = [CONF_COLD_WHITE_COLOR_TEMPERATURE, CONF_WARM_WHITE_COLOR_TEMPERATURE]
_DUAL_ADDRESS_KEYS = [CONF_COLD_WHITE_ADDRESS, CONF_WARM_WHITE_ADDRESS]


def _validate_config(value):
    has_dual_addr = all(k in value for k in _DUAL_ADDRESS_KEYS)
    has_partial_dual = any(k in value for k in _DUAL_ADDRESS_KEYS) and not has_dual_addr
    has_single = any(k in value for k in _SINGLE_ADDRESS_KEYS)
    has_ct_temps = all(k in value for k in _CT_TEMP_KEYS)
    has_partial_ct = any(k in value for k in _CT_TEMP_KEYS) and not has_ct_temps

    if has_partial_dual:
        raise cv.Invalid(
            f"{CONF_COLD_WHITE_ADDRESS} and {CONF_WARM_WHITE_ADDRESS} must be set together",
        )

    if has_partial_ct:
        raise cv.Invalid(
            f"{CONF_COLD_WHITE_COLOR_TEMPERATURE} and "
            f"{CONF_WARM_WHITE_COLOR_TEMPERATURE} must be set together",
        )

    if has_dual_addr and has_single:
        raise cv.Invalid(
            "Use either single-address options (address, group, broadcast) "
            "or dual-channel addresses (cold_white_address, warm_white_address), not both",
        )

    if has_ct_temps and not (has_single or has_dual_addr):
        raise cv.Invalid(
            "colour temperature limits require a single address or dual-channel addresses",
        )

    if has_dual_addr and not has_ct_temps:
        raise cv.Invalid(
            "dual-channel CT requires cold_white_color_temperature and "
            "warm_white_color_temperature for the HA colour-temperature range",
        )

    return value


CONFIG_SCHEMA = cv.All(
    light.RGB_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(DALILight),
            cv.GenerateID(CONF_INTERFACE): cv.use_id(DALIInterface),
            cv.Optional(CONF_ADDRESS): cv.int_range(min=0, max=63),
            cv.Optional(CONF_GROUP): cv.int_range(min=0, max=15),
            cv.Optional(CONF_BROADCAST): cv.boolean,
            cv.Optional(CONF_COLD_WHITE_ADDRESS): cv.int_range(min=0, max=63),
            cv.Optional(CONF_WARM_WHITE_ADDRESS): cv.int_range(min=0, max=63),
            cv.Optional(CONF_FADERATE): cv.int_range(min=0, max=15),
            cv.Optional(CONF_FADETIME): cv.int_range(min=0, max=15),
            cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
            cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
        }
    ),
    cv.has_at_most_one_key(*_SINGLE_ADDRESS_KEYS),
    light.validate_color_temperature_channels,
    _validate_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    await cg.register_component(var, config)

    interface = await cg.get_variable(config[CONF_INTERFACE])
    cg.add(var.set_interface(interface))

    if CONF_COLD_WHITE_ADDRESS in config:
        cg.add(var.set_ct_mode(CtMode.DUAL_CHANNEL))
        cg.add(var.set_cold_white_address(config[CONF_COLD_WHITE_ADDRESS]))
        cg.add(var.set_warm_white_address(config[CONF_WARM_WHITE_ADDRESS]))
    else:
        if CONF_ADDRESS in config:
            cg.add(var.set_address(config[CONF_ADDRESS]))
        elif CONF_GROUP in config:
            cg.add(var.set_group(config[CONF_GROUP]))
        elif config.get(CONF_BROADCAST, False):
            cg.add(var.set_broadcast())

        if CONF_COLD_WHITE_COLOR_TEMPERATURE in config:
            cg.add(var.set_ct_mode(CtMode.DT8))

    if CONF_FADERATE in config:
        cg.add(var.set_fade_rate(config[CONF_FADERATE]))
    if CONF_FADETIME in config:
        cg.add(var.set_fade_time(config[CONF_FADETIME]))

    if CONF_COLD_WHITE_COLOR_TEMPERATURE in config:
        cg.add(var.set_min_mireds(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
        cg.add(var.set_max_mireds(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
