import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
import esphome.final_validate as fv

from esphome.const import (
   CONF_PLATFORM,
   ENTITY_CATEGORY_CONFIG,
)

from ..audio_dac import (
    CONF_TAS58XX_ID,
    NUMBER_PEQ_BANDS,
    ROLE_MID,
    ROLE_TWEETER,
    Tas58xxComponent,
    tas58xx_ns,
    validate_crossover_entities,
)

EqModeSelect = tas58xx_ns.class_("EqModeSelect", select.Select, cg.Component)
MixerModeSelect = tas58xx_ns.class_("MixerModeSelect", select.Select, cg.Component)
EqPresetLeftSelect = tas58xx_ns.class_("EqPresetLeftSelect", select.Select, cg.Component)
EqPresetRightSelect = tas58xx_ns.class_("EqPresetRightSelect", select.Select, cg.Component)
CrossoverSelect = tas58xx_ns.class_("CrossoverSelect", select.Select, cg.Component)
CrossoverParam = tas58xx_ns.enum("CrossoverParam")

# crossover selects, all need audio_dac crossover:
# options are in the same order as the C++ enums in tas58xx_crossover.h
# key: (C++ CrossoverParam, driver role, PEQ band, options)
CROSSOVER_FILTER_OPTIONS = ["LR2 12dB/oct", "LR4 24dB/oct", "Butterworth 12dB/oct", "Butterworth 24dB/oct"]
CROSSOVER_CHANNEL_OPTIONS = ["Left", "Right"]
PEQ_TYPE_OPTIONS = ["Peaking", "Low Shelf", "High Shelf"]
BAFFLE_PLACEMENT_OPTIONS = ["Freestanding", "Near Wall", "Corner"]

CROSSOVER_SELECTS = {
    "crossover_filter": ("XO_PARAM_FILTER", ROLE_TWEETER, 0, CROSSOVER_FILTER_OPTIONS),
    "crossover_tweeter_channel": ("XO_PARAM_TWEETER_CHANNEL", ROLE_TWEETER, 0, CROSSOVER_CHANNEL_OPTIONS),
    "baffle_placement": ("XO_PARAM_BAFFLE_PLACEMENT", ROLE_MID, 0, BAFFLE_PLACEMENT_OPTIONS),
}
for _role, _prefix in ((ROLE_TWEETER, "tweeter"), (ROLE_MID, "mid")):
    for _band in range(NUMBER_PEQ_BANDS):
        CROSSOVER_SELECTS[f"{_prefix}_peq_{_band + 1}_type"] = ("XO_PARAM_PEQ_TYPE", _role, _band, PEQ_TYPE_OPTIONS)

CONF_EQ_MODE = "eq_mode"
CONF_MIXER_MODE = "mixer_mode"
CONF_EQ_PRESET_LEFT_CHANNEL = "eq_preset_left_channel"
CONF_EQ_PRESET_RIGHT_CHANNEL = "eq_preset_right_channel"

NUMBER_COMPONENT = "number"
PLATFORM_TAS58XX = "tas58xx"
LEFT_EQ_GAIN_20HZ = "left_eq_gain_20Hz"

def validate_eq_presets(config):
    have_select_eq_mode = CONF_EQ_MODE in config
    have_eq_preset_left = CONF_EQ_PRESET_LEFT_CHANNEL in config
    have_eq_preset_right = CONF_EQ_PRESET_RIGHT_CHANNEL in config

    if have_eq_preset_left and not have_eq_preset_right:
        raise cv.Invalid("Select eq_preset_right must configured with eq_preset_left - add configuration for Select eq_preset_right")

    if have_eq_preset_right and not have_eq_preset_left:
        raise cv.Invalid("Select eq_preset_left must configured with eq_preset_right - add configuration for Select eq_preset_left")

    if not have_select_eq_mode and (have_eq_preset_left or have_eq_preset_right):
         raise cv.Invalid("Select eq_mode must configured with both left and right eq_presets - add configuration for Select eq_mode")

    return config

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TAS58XX_ID): cv.use_id(Tas58xxComponent),
        cv.Optional(CONF_EQ_MODE): select.select_schema(
            EqModeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
         ),
        cv.Optional(CONF_MIXER_MODE): select.select_schema(
            MixerModeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_EQ_PRESET_LEFT_CHANNEL): select.select_schema(
            EqPresetLeftSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_EQ_PRESET_RIGHT_CHANNEL): select.select_schema(
            EqPresetRightSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        **{
            cv.Optional(key): select.select_schema(
                CrossoverSelect,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:tune-variant",
            ).extend(cv.COMPONENT_SCHEMA)
            for key in CROSSOVER_SELECTS
        },
    }
).add_extra(validate_eq_presets)

def _final_validate(config):
    have_defined_tas58xx_number_eq_gain = False

    have_select_eq_mode = CONF_EQ_MODE in config
    have_defined_tas58xx_select_eq_preset = (CONF_EQ_PRESET_LEFT_CHANNEL in config)

    full_conf = fv.full_config.get()
    number_confs = full_conf.get(NUMBER_COMPONENT, [])
    for number_conf in number_confs:
        if number_conf.get(CONF_PLATFORM) == PLATFORM_TAS58XX:
           if LEFT_EQ_GAIN_20HZ in number_conf:
               have_defined_tas58xx_number_eq_gain = True
               break

    if (have_defined_tas58xx_select_eq_preset and have_defined_tas58xx_number_eq_gain):
        raise cv.Invalid("Select eq_presets are not allowed with Number left_eq_gains and/or right_eq_gains - remove one set of those configurations")

    if (not have_select_eq_mode and have_defined_tas58xx_number_eq_gain):
        raise cv.Invalid("Select eq_mode is required with Number left_eq_gains - add Select eq_mode to YAML configuration")

    validate_crossover_entities(config, CROSSOVER_SELECTS)
    return config

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):
    tas58xx_component = await cg.get_variable(config[CONF_TAS58XX_ID])
    if eq_mode_config := config.get(CONF_EQ_MODE):
        s = await select.new_select(
            eq_mode_config,
            options=[],
        )
        await cg.register_component(s, eq_mode_config)
        await cg.register_parented(s, tas58xx_component)

    if mixer_mode_config := config.get(CONF_MIXER_MODE):
        s = await select.new_select(
            mixer_mode_config,
            options=[],
        )
        await cg.register_component(s, mixer_mode_config)
        await cg.register_parented(s, tas58xx_component)

    if eq_preset_left_config := config.get(CONF_EQ_PRESET_LEFT_CHANNEL):
        cg.add_define("USE_TAS58XX_EQ_PRESETS")
        s = await select.new_select(
            eq_preset_left_config,
            options=[],
        )
        await cg.register_component(s, eq_preset_left_config)
        await cg.register_parented(s, tas58xx_component)

    if eq_preset_right_config := config.get(CONF_EQ_PRESET_RIGHT_CHANNEL):
        s = await select.new_select(
            eq_preset_right_config,
            options=[],
        )
        await cg.register_component(s, eq_preset_right_config)
        await cg.register_parented(s, tas58xx_component)

    for key, (param, role, band, options) in CROSSOVER_SELECTS.items():
        if crossover_config := config.get(key):
            s = await select.new_select(crossover_config, options=options)
            await cg.register_component(s, crossover_config)
            await cg.register_parented(s, tas58xx_component)
            cg.add(s.set_crossover_param(getattr(CrossoverParam, param), role, band))
