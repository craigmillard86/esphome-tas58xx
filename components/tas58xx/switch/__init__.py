import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_SWITCH, ENTITY_CATEGORY_CONFIG

CONF_ENABLE_DAC = "enable_dac"
# CONF_ENABLE_EQ = "enable_eq"

from ..audio_dac import (
    CONF_TAS58XX_ID,
    ROLE_MID,
    ROLE_TWEETER,
    Tas58xxComponent,
    tas58xx_ns,
    validate_crossover_entities,
)

EnableDacSwitch = tas58xx_ns.class_("EnableDacSwitch", switch.Switch, cg.Component)
CrossoverSwitch = tas58xx_ns.class_("CrossoverSwitch", switch.Switch, cg.Component)
CrossoverParam = tas58xx_ns.enum("CrossoverParam")

# crossover switches, all need audio_dac crossover:
# key: (C++ CrossoverParam, driver role, icon)
CROSSOVER_SWITCHES = {
    "tweeter_invert": ("XO_PARAM_INVERT", ROLE_TWEETER, "mdi:swap-vertical"),
    "mid_invert": ("XO_PARAM_INVERT", ROLE_MID, "mdi:swap-vertical"),
    "loudness": ("XO_PARAM_LOUDNESS", ROLE_TWEETER, "mdi:volume-plus"),
    # mute one driver to measure the other, always off at boot
    "tweeter_mute": ("XO_PARAM_MUTE", ROLE_TWEETER, "mdi:volume-off"),
    "mid_mute": ("XO_PARAM_MUTE", ROLE_MID, "mdi:volume-off"),
    "headroom_compensation": ("XO_PARAM_HEADROOM_COMPENSATION", ROLE_TWEETER, "mdi:arrow-collapse-vertical"),
}
# EnableEqSwitch = tas58xx_ns.class_("EnableEqSwitch", switch.Switch , cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TAS58XX_ID): cv.use_id(Tas58xxComponent),

        cv.Optional(CONF_ENABLE_DAC): switch.switch_schema(
            EnableDacSwitch,
            device_class=DEVICE_CLASS_SWITCH,
        )
        .extend(cv.COMPONENT_SCHEMA),

        # cv.Optional(CONF_ENABLE_EQ): switch.switch_schema(
        #     EnableEqSwitch,
        #     device_class=DEVICE_CLASS_SWITCH,
        # )
        # .extend(cv.COMPONENT_SCHEMA),

        **{
            cv.Optional(key): switch.switch_schema(
                CrossoverSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon=icon,
            ).extend(cv.COMPONENT_SCHEMA)
            for key, (_, _, icon) in CROSSOVER_SWITCHES.items()
        },
    }
)

def _final_validate(config):
    validate_crossover_entities(config, CROSSOVER_SWITCHES)
    return config

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):
  tas58xx_component = await cg.get_variable(config[CONF_TAS58XX_ID])
  if enable_dac_config := config.get(CONF_ENABLE_DAC):
    s = await switch.new_switch(enable_dac_config)
    await cg.register_component(s, enable_dac_config)
    await cg.register_parented(s, tas58xx_component)

#   if enable_eq_config := config.get(CONF_ENABLE_EQ):
#     s = await switch.new_switch(enable_eq_config)
#     await cg.register_component(s, enable_eq_config)
#     await cg.register_parented(s, tas58xx_component)


  for key, (param, role, _) in CROSSOVER_SWITCHES.items():
    if crossover_config := config.get(key):
      s = await switch.new_switch(crossover_config)
      await cg.register_component(s, crossover_config)
      await cg.register_parented(s, tas58xx_component)
      cg.add(s.set_crossover_param(getattr(CrossoverParam, param), role))
