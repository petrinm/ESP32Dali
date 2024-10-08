import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome.const import CONF_ID

from .. import DALI_ns, DALIInterface
assert __package__ == "esphome.components.dali.output"


# Names for configuration
CONF_INTERFACE = 'interface'
CONF_ADDRESS = 'address'
CONF_GROUP = 'group'
CONF_FADERATE = 'faderate'
CONF_FADETIME = 'faderate'


# Setup output class
DALI_Output = DALI_ns.class_('DALIOutput', output.FloatOutput, cg.Component)

# Setup Config Schema for yaml
CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend({
    cv.Required(CONF_ID): cv.declare_id(DALI_Output),
    cv.GenerateID(CONF_INTERFACE): cv.use_id(DALIInterface),
    cv.Optional(CONF_ADDRESS): cv.int_range(min=0, max=63),
    cv.Optional(CONF_GROUP): cv.int_range(min=0, max=15),
    cv.Optional(CONF_FADERATE): cv.int_range(min=0, max=255),
    cv.Optional(CONF_FADETIME): cv.int_range(min=0, max=255),
}).extend(cv.COMPONENT_SCHEMA)



async def to_code(config):

    var = cg.new_Pvariable(config[CONF_ID])
    await output.register_output(var, config)
    await cg.register_component(var, config)

    # Set Parent
    interface = await cg.get_variable(config[CONF_INTERFACE])
    cg.add(var.set_interface(interface))

    # Address & group
    if CONF_ADDRESS in config and CONF_GROUP in config:
        raise ValueError("Both address and group defined!")
    if CONF_ADDRESS in config:
        cg.add(var.set_address(config[CONF_ADDRESS]))
    if CONF_GROUP in config:
        cg.add(var.set_group(config[CONF_GROUP]))

    # Faderate & fadetime
    if CONF_FADERATE in config:
        cg.add(var.set_fade_rate(config[CONF_FADERATE]))
    if CONF_FADETIME in config:
        cg.add(var.set_fade_time(config[CONF_FADETIME]))
