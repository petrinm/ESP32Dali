import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_TYPE,
    DEVICE_CLASS_IDENTIFY,
    ENTITY_CATEGORY_CONFIG,
)

from . import DALI_ns, DALIInterface

DEPENDENCIES = ["button"]

CONF_INTERFACE = "interface"
CONF_START_SHORT_ADDRESS = "start_short_address"
CONF_MAX_DEVICES = "max_devices"
CONF_ASSIGN_ADDRESSES = "assign_addresses"

TYPE_IDENTIFY = "identify"
TYPE_COMMISSION = "commission"

DALIIdentifyButton = DALI_ns.class_("DALIIdentifyButton", button.Button, cg.Component)
DALICommissionButton = DALI_ns.class_("DALICommissionButton", button.Button, cg.Component)

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_IDENTIFY: button.button_schema(
            DALIIdentifyButton,
            icon="mdi:lightbulb-spot",
            device_class=DEVICE_CLASS_IDENTIFY,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ).extend(
            {
                cv.GenerateID(CONF_INTERFACE): cv.use_id(DALIInterface),
                cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=63),
            },
            cv.COMPONENT_SCHEMA,
        ),
        TYPE_COMMISSION: button.button_schema(
            DALICommissionButton,
            icon="mdi:lan-connect",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ).extend(
            {
                cv.GenerateID(CONF_INTERFACE): cv.use_id(DALIInterface),
                cv.Optional(CONF_START_SHORT_ADDRESS, default=0): cv.int_range(min=0, max=63),
                cv.Optional(CONF_MAX_DEVICES, default=64): cv.int_range(min=1, max=64),
                cv.Optional(CONF_ASSIGN_ADDRESSES, default=True): cv.boolean,
            },
            cv.COMPONENT_SCHEMA,
        ),
    },
    lower=True,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)

    interface = await cg.get_variable(config[CONF_INTERFACE])
    cg.add(var.set_interface(interface))

    if config[CONF_TYPE] == TYPE_IDENTIFY:
        cg.add(var.set_address(config[CONF_ADDRESS]))
    else:
        cg.add(var.set_start_short_address(config[CONF_START_SHORT_ADDRESS]))
        cg.add(var.set_max_devices(config[CONF_MAX_DEVICES]))
        cg.add(var.set_assign_addresses(config[CONF_ASSIGN_ADDRESSES]))
