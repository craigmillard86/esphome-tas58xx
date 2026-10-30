import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from ..audio_dac import CONF_TAS58XX_ID, Tas58xxComponent, tas58xx_ns, validate_crossover_entities

CrossoverButton = tas58xx_ns.class_("CrossoverButton", button.Button, cg.Component)
CrossoverAction = tas58xx_ns.enum("CrossoverButtonAction")

# crossover buttons, all need audio_dac crossover:
# key: (C++ action, icon)
CROSSOVER_BUTTONS = {
    # log the current settings as a crossover: YAML block to copy to another cabinet
    "crossover_export": ("XO_BUTTON_EXPORT", "mdi:export"),
    # go back to the crossover: YAML values, replacing anything saved from Home Assistant
    "crossover_reset": ("XO_BUTTON_RESET", "mdi:restore"),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TAS58XX_ID): cv.use_id(Tas58xxComponent),
        **{
            cv.Optional(key): button.button_schema(
                CrossoverButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon=icon,
            ).extend(cv.COMPONENT_SCHEMA)
            for key, (_, icon) in CROSSOVER_BUTTONS.items()
        },
    }
)

def _final_validate(config):
    validate_crossover_entities(config, CROSSOVER_BUTTONS)
    return config

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):
    tas58xx_component = await cg.get_variable(config[CONF_TAS58XX_ID])
    for key, (action, _) in CROSSOVER_BUTTONS.items():
        if button_config := config.get(key):
            b = await button.new_button(button_config)
            await cg.register_component(b, button_config)
            await cg.register_parented(b, tas58xx_component)
            cg.add(b.set_action(getattr(CrossoverAction, action)))
