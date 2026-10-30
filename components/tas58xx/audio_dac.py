import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.core import CORE
from esphome.components import i2c
from esphome.components.audio_dac import AudioDac
from esphome import pins

from esphome.const import (
    CONF_ADDRESS,
    CONF_ENABLE_PIN,
    CONF_FREQUENCY,
    CONF_GAIN,
    CONF_ID,
    CONF_PLATFORM,
    CONF_TYPE,
)

CODEOWNERS = ["@mrtoy-me"]
DEPENDENCIES = ["i2c"]

# yaml configuration constants
CONF_ANALOG_GAIN = "analog_gain"
CONF_AUDIO_DAC = "audio_dac"
CONF_DAC_MODE = "dac_mode"
CONF_MODULATION = "modulation"
CONF_TAS58XX_DAC = "tas58xx_dac"
CONF_IGNORE_FAULT = "ignore_fault"
CONF_MIXER_MODE = "mixer_mode"
CONF_REFRESH_EQ = "refresh_eq"
CONF_VOLUME_MIN = "volume_min"
CONF_VOLUME_MAX = "volume_max"
CONF_TAS58XX_ID = "tas58xx_id"

# crossover yaml configuration constants
CONF_CROSSOVER = "crossover"
CONF_FILTER = "filter"
CONF_TWEETER_CHANNEL = "tweeter_channel"
CONF_DSP_SAMPLE_RATE = "dsp_sample_rate"
CONF_TWEETER_GAIN = "tweeter_gain"
CONF_MID_GAIN = "mid_gain"
CONF_TWEETER_INVERT = "tweeter_invert"
CONF_MID_INVERT = "mid_invert"
CONF_TWEETER_DELAY = "tweeter_delay"
CONF_MID_DELAY = "mid_delay"
CONF_TWEETER_PEQ = "tweeter_peq"
CONF_MID_PEQ = "mid_peq"
CONF_LOUDNESS = "loudness"
CONF_LOUDNESS_REFERENCE = "loudness_reference"
CONF_Q = "q"
CONF_SUBSONIC_FREQUENCY = "subsonic_frequency"
CONF_BAFFLE_WIDTH = "baffle_width"
CONF_BAFFLE_PLACEMENT = "baffle_placement"
CONF_TWEETER_NOTCH = "tweeter_notch"
CONF_AIR_GAIN = "air_gain"
CONF_TWEETER_MIN_FREQUENCY = "tweeter_min_frequency"
CONF_LINKWITZ = "linkwitz"
CONF_F0 = "f0"
CONF_Q0 = "q0"
CONF_FP = "fp"
CONF_QP = "qp"
CONF_HEADROOM_COMPENSATION = "headroom_compensation"

# used for looking through CORE.config to derive eq configuration
NUMBER_COMPONENT= "number"
SELECT_COMPONENT = "select"
PLATFORM_TAS58XX = "tas58xx"
LEFT_EQ_GAIN_20HZ = "left_eq_gain_20Hz"
RIGHT_EQ_GAIN_20HZ = "right_eq_gain_20Hz"
EQ_PRESET_LEFT_CHANNEL = "eq_preset_left_channel"

# eq mode enum and select index values
EQ_OFF = 0
EQ_15BAND = 1
EQ_BIAMP = 2
EQ_PRESETS = 3
EQ_CROSSOVER = 4

# crossover enum index values, must match tas58xx_crossover.h
CROSSOVER_FILTERS = {"LR2": 0, "LR4": 1, "BW2": 2, "BW4": 3}
CROSSOVER_CHANNELS = {"LEFT": 0, "RIGHT": 1}
PEQ_TYPES = {"PEAKING": 0, "LOW_SHELF": 1, "HIGH_SHELF": 2}
BAFFLE_PLACEMENTS = {"FREESTANDING": 0, "NEAR_WALL": 1, "CORNER": 2}
ROLE_TWEETER = 0
ROLE_MID = 1
NUMBER_PEQ_BANDS = 6

# used to check crossover entities are only configured with a crossover
SWITCH_COMPONENT = "switch"
EQ_MODE = "eq_mode"

# i2c addresses of dac models
TAS5805M_I2C_ADDR = 0x2D
TAS5825M_I2C_ADDR = 0x4C

tas58xx_ns = cg.esphome_ns.namespace("tas58xx")
Tas58xxComponent = tas58xx_ns.class_("Tas58xxComponent", AudioDac, cg.PollingComponent, i2c.I2CDevice)

EqRefreshMode = tas58xx_ns.enum("EqRefreshMode")
EQ_REFRESH_MODES = {
     "AUTO"  : EqRefreshMode.AUTO,
     "MANUAL": EqRefreshMode.MANUAL,
}

TasDac = tas58xx_ns.enum("TasDac")
TAS_DACS = {
    "TAS5805M" : TasDac.TAS5805M,
    "TAS5825M" : TasDac.TAS5825M,
}

DacMode = tas58xx_ns.enum("DacMode")
DAC_MODES = {
    "BTL"  : DacMode.BTL,
    "PBTL" : DacMode.PBTL,
}

ModulationScheme = tas58xx_ns.enum("ModulationScheme")
MODULATION_SCHEMES = {
    "BD_MODE"   : ModulationScheme.MODE_BD,
    "1SPW_MODE" : ModulationScheme.MODE_1SPW,
}

ExcludeIgnoreMode = tas58xx_ns.enum("ExcludeIgnoreModes")
EXCLUDE_IGNORE_MODES = {
     "NONE"        : ExcludeIgnoreMode.NONE,
     "CLOCK_FAULT" : ExcludeIgnoreMode.CLOCK_FAULT,
}

MixerMode = tas58xx_ns.enum("MixerMode")
MIXER_MODES = {
    "STEREO"         : MixerMode.STEREO,
    "STEREO_INVERSE" : MixerMode.STEREO_INVERSE,
    "MONO"           : MixerMode.MONO,
    "RIGHT"          : MixerMode.RIGHT,
    "LEFT"           : MixerMode.LEFT,
}

ANALOG_GAINS = [-15.5, -15, -14.5, -14, -13.5, -13, -12.5, -12, -11.5, -11, -10.5, -10, -9.5, -9, -8.5, -8,
                 -7.5,  -7,  -6.5,  -6,  -5.5,  -5,  -4.5,  -4,  -3.5,  -3,  -2.5,  -2, -1.5, -1, -0.5,  0]

def zero_or_range(min_value, max_value):
    # 0 turns the filter off, any other value must be in range
    range_validator = cv.float_range(min=min_value, max=max_value)

    def validator(value):
        if value == 0:
            return value
        try:
            return range_validator(value)
        except cv.Invalid as err:
            raise cv.Invalid(f"must be 0 (off) or between {min_value} and {max_value}") from err

    return validator

PEQ_BAND_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_FREQUENCY, default="1000Hz"): cv.All(cv.frequency, cv.float_range(min=20, max=20000)),
        cv.Optional(CONF_GAIN, default="0dB"): cv.All(cv.decibel, cv.float_range(min=-15, max=15)),
        cv.Optional(CONF_Q, default=1.0): cv.float_range(min=0.1, max=10),
        cv.Optional(CONF_TYPE, default="PEAKING"): cv.enum(PEQ_TYPES, upper=True, space="_"),
    }
)

NOTCH_SCHEMA = cv.Schema(
    {
        # frequency 0 or gain 0dB turns the notch off
        cv.Optional(CONF_FREQUENCY, default="0Hz"): cv.All(cv.frequency, zero_or_range(1000, 20000)),
        cv.Optional(CONF_GAIN, default="0dB"): cv.All(cv.decibel, cv.float_range(min=-24, max=0)),
        cv.Optional(CONF_Q, default=5.0): cv.float_range(min=0.5, max=20),
    }
)

LINKWITZ_SCHEMA = cv.Schema(
    {
        # measured sealed box resonance f0/q0 and target fp/qp, a frequency of 0 turns it off
        cv.Optional(CONF_F0, default="0Hz"): cv.All(cv.frequency, zero_or_range(15, 200)),
        cv.Optional(CONF_Q0, default=0.707): cv.float_range(min=0.3, max=2),
        cv.Optional(CONF_FP, default="0Hz"): cv.All(cv.frequency, zero_or_range(15, 200)),
        cv.Optional(CONF_QP, default=0.5): cv.float_range(min=0.3, max=2),
    }
)

def validate_crossover(config):
    if config[CONF_FREQUENCY] < config[CONF_TWEETER_MIN_FREQUENCY]:
        raise cv.Invalid(f"{CONF_FREQUENCY} must not be below {CONF_TWEETER_MIN_FREQUENCY}")
    linkwitz = config[CONF_LINKWITZ]
    if (linkwitz[CONF_F0] == 0) != (linkwitz[CONF_FP] == 0):
        raise cv.Invalid(f"{CONF_LINKWITZ} needs both {CONF_F0} and {CONF_FP}, or neither")
    return config

CROSSOVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_FILTER, default="LR4"): cv.enum(CROSSOVER_FILTERS, upper=True),
        cv.Optional(CONF_FREQUENCY, default="2500Hz"): cv.All(cv.frequency, cv.float_range(min=500, max=8000)),
        # lowest crossover frequency the tweeter may ever get, also limits the frequency number
        cv.Optional(CONF_TWEETER_MIN_FREQUENCY, default="500Hz"): cv.All(cv.frequency, cv.float_range(min=500, max=8000)),
        cv.Optional(CONF_TWEETER_CHANNEL, default="LEFT"): cv.enum(CROSSOVER_CHANNELS, upper=True),
        cv.Optional(CONF_TWEETER_GAIN, default="0dB"): cv.All(cv.decibel, cv.float_range(min=-24, max=6)),
        cv.Optional(CONF_MID_GAIN, default="0dB"): cv.All(cv.decibel, cv.float_range(min=-24, max=6)),
        cv.Optional(CONF_TWEETER_INVERT, default=False): cv.boolean,
        cv.Optional(CONF_MID_INVERT, default=False): cv.boolean,
        # physical driver offset in mm, applied as a 1st order all-pass approximation of a delay
        cv.Optional(CONF_TWEETER_DELAY, default=0): cv.float_range(min=0, max=50),
        cv.Optional(CONF_MID_DELAY, default=0): cv.float_range(min=0, max=50),
        cv.Optional(CONF_TWEETER_PEQ, default=[]): cv.All(cv.ensure_list(PEQ_BAND_SCHEMA), cv.Length(max=NUMBER_PEQ_BANDS)),
        cv.Optional(CONF_MID_PEQ, default=[]): cv.All(cv.ensure_list(PEQ_BAND_SCHEMA), cv.Length(max=NUMBER_PEQ_BANDS)),
        # mid: subsonic high-pass (0 is off, else 10Hz to 100Hz) and baffle step (width 0 is off)
        cv.Optional(CONF_SUBSONIC_FREQUENCY, default="0Hz"): cv.All(cv.frequency, zero_or_range(10, 100)),
        cv.Optional(CONF_BAFFLE_WIDTH, default=0): zero_or_range(5, 50),
        cv.Optional(CONF_BAFFLE_PLACEMENT, default="FREESTANDING"): cv.enum(BAFFLE_PLACEMENTS, upper=True, space="_"),
        # tweeter: breakup notch and air shelf at 10kHz
        cv.Optional(CONF_TWEETER_NOTCH, default={}): NOTCH_SCHEMA,
        cv.Optional(CONF_AIR_GAIN, default="0dB"): cv.All(cv.decibel, cv.float_range(min=-6, max=6)),
        cv.Optional(CONF_LINKWITZ, default={}): LINKWITZ_SCHEMA,
        cv.Optional(CONF_HEADROOM_COMPENSATION, default=False): cv.boolean,
        cv.Optional(CONF_LOUDNESS, default=False): cv.boolean,
        cv.Optional(CONF_LOUDNESS_REFERENCE, default="0dB"): cv.All(cv.decibel, cv.float_range(min=-60, max=0)),
        # rate the DSP biquads run at, 96kHz with the startup registers used by this component
        cv.Optional(CONF_DSP_SAMPLE_RATE, default="96000Hz"): cv.All(cv.frequency, cv.one_of(48000, 96000)),
    }
).add_extra(validate_crossover)

def validate_config(config):
    if config[CONF_DAC_MODE] == "PBTL" and (config[CONF_MIXER_MODE] == "STEREO" or config[CONF_MIXER_MODE] == "STEREO_INVERSE"):
        raise cv.Invalid("dac_mode: PBTL must have mixer_mode: MONO or RIGHT or LEFT")
    if CONF_CROSSOVER in config and config[CONF_DAC_MODE] != "BTL":
        raise cv.Invalid("crossover: requires dac_mode: BTL, one channel drives the tweeter and the other the mid")
    if (config[CONF_VOLUME_MAX] - config[CONF_VOLUME_MIN]) < 9:
        raise cv.Invalid("volume_max must at least 9db greater than volume_min")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Tas58xxComponent),
            cv.Required(CONF_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_TAS58XX_DAC, default="TAS5805M"): cv.enum(
                        TAS_DACS, upper=True
            ),
            cv.Optional(CONF_ANALOG_GAIN, default="-15.5dB"): cv.All(
                        cv.decibel, cv.one_of(*ANALOG_GAINS)
            ),
            cv.Optional(CONF_DAC_MODE, default="BTL"): cv.enum(
                        DAC_MODES, upper=True
            ),
            cv.Optional(CONF_MODULATION, default="BD_MODE"): cv.enum(
                        MODULATION_SCHEMES, upper=True
            ),
            cv.Optional(CONF_IGNORE_FAULT, default="CLOCK_FAULT"): cv.enum(
                        EXCLUDE_IGNORE_MODES, upper=True
            ),
            cv.Optional(CONF_MIXER_MODE, default="STEREO"): cv.enum(
                        MIXER_MODES, upper=True
            ),
            cv.Optional(CONF_REFRESH_EQ, default="AUTO"): cv.enum(
                        EQ_REFRESH_MODES, upper=True
            ),
            cv.Optional(CONF_VOLUME_MAX, default="24dB"): cv.All(
                        cv.decibel, cv.int_range(-103, 24)
            ),
            cv.Optional(CONF_VOLUME_MIN, default="-103dB"): cv.All(
                        cv.decibel, cv.int_range(-103, 24)
            ),
            cv.Optional(CONF_CROSSOVER): CROSSOVER_SCHEMA,
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(i2c.i2c_device_schema(TAS5805M_I2C_ADDR))
    .add_extra(validate_config),
    cv.only_on_esp32,
)

def _final_validate(config):
    full_conf = fv.full_config.get()

    tas58xx_numbers = [n for n in full_conf.get(NUMBER_COMPONENT, []) if n.get(CONF_PLATFORM) == PLATFORM_TAS58XX]
    tas58xx_selects = [s for s in full_conf.get(SELECT_COMPONENT, []) if s.get(CONF_PLATFORM) == PLATFORM_TAS58XX]

    have_eq_gains = any(LEFT_EQ_GAIN_20HZ in n or RIGHT_EQ_GAIN_20HZ in n for n in tas58xx_numbers)
    have_eq_presets = any(EQ_PRESET_LEFT_CHANNEL in s for s in tas58xx_selects)
    have_eq_mode_select = any(EQ_MODE in s for s in tas58xx_selects)

    if CONF_CROSSOVER in config:
        if have_eq_gains or have_eq_presets:
            raise cv.Invalid("crossover: is not allowed with EQ gain numbers or EQ preset selects - remove one of those configurations")
        if config[CONF_REFRESH_EQ] == "MANUAL" and not have_eq_mode_select:
            raise cv.Invalid("crossover: with refresh_eq: MANUAL requires Select eq_mode to trigger the refresh")
    return config

FINAL_VALIDATE_SCHEMA = _final_validate

def crossover_tweeter_min_frequency():
    # used by the frequency number so Home Assistant cannot go below the YAML minimum
    for dac in CORE.config.get(CONF_AUDIO_DAC, []):
        if dac.get(CONF_PLATFORM) == PLATFORM_TAS58XX and CONF_CROSSOVER in dac:
            return dac[CONF_CROSSOVER][CONF_TWEETER_MIN_FREQUENCY]
    return 500

def validate_crossover_entities(config, keys):
    # used by number, select and switch platforms: crossover entities need audio_dac crossover:
    configured = [key for key in keys if key in config]
    if not configured:
        return
    full_conf = fv.full_config.get()
    for dac in full_conf.get(CONF_AUDIO_DAC, []):
        if dac.get(CONF_PLATFORM) == PLATFORM_TAS58XX and CONF_CROSSOVER in dac:
            return
    raise cv.Invalid(f"{configured[0]} requires crossover: under audio_dac: platform: tas58xx")

def left_eq_gain_exists():
    all_numbers = CORE.config.get(NUMBER_COMPONENT, [])
    for num in all_numbers:
        if num.get(CONF_PLATFORM) == PLATFORM_TAS58XX:
            if LEFT_EQ_GAIN_20HZ in num:
                return True

    return False

def right_eq_gain_exists():
    all_numbers = CORE.config.get(NUMBER_COMPONENT, [])
    for num in all_numbers:
        if num.get(CONF_PLATFORM) == PLATFORM_TAS58XX:
            if RIGHT_EQ_GAIN_20HZ in num:
                return True

    return False

def select_eq_presets_exists():
    all_select = CORE.config.get(SELECT_COMPONENT, [])
    for select in all_select:
        if select.get(CONF_PLATFORM) == PLATFORM_TAS58XX:
            if EQ_PRESET_LEFT_CHANNEL in select:
                return True

    return False

async def to_code(config):
    derived_eq_mode_configuration = EQ_OFF
    if CONF_CROSSOVER in config:
        derived_eq_mode_configuration = EQ_CROSSOVER
    elif right_eq_gain_exists():
        derived_eq_mode_configuration  = EQ_BIAMP
    else:
      if left_eq_gain_exists():
          derived_eq_mode_configuration = EQ_15BAND
      else:
        if select_eq_presets_exists():
            derived_eq_mode_configuration = EQ_PRESETS

    tas58xx_dac = config.get(CONF_TAS58XX_DAC)
    if tas58xx_dac == "TAS5825M":
        config[CONF_ADDRESS] = TAS5825M_I2C_ADDR

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    enable = await cg.gpio_pin_expression(config[CONF_ENABLE_PIN])
    cg.add(var.set_enable_pin(enable))
    cg.add(var.config_analog_gain(config[CONF_ANALOG_GAIN]))
    cg.add(var.config_dac_mode(config[CONF_DAC_MODE]))
    cg.add(var.config_modulation_scheme(config[CONF_MODULATION]))
    cg.add(var.config_ignore_fault_mode(config[CONF_IGNORE_FAULT]))
    cg.add(var.config_mixer_mode(config[CONF_MIXER_MODE]))
    cg.add(var.config_refresh_eq(config[CONF_REFRESH_EQ]))
    cg.add(var.config_volume_max(config[CONF_VOLUME_MAX]))
    cg.add(var.config_volume_min(config[CONF_VOLUME_MIN]))
    cg.add(var.config_eq_mode(derived_eq_mode_configuration))

    if config[CONF_DAC_MODE] == "PBTL":
        cg.add_define("USE_DAC_MODE_PBTL")

    if tas58xx_dac == "TAS5805M":
        cg.add_define("USE_TAS5805M_DAC")
    else:
        cg.add_define("USE_TAS5825M_DAC")

    if crossover_config := config.get(CONF_CROSSOVER):
        cg.add_define("USE_TAS58XX_EQ_CROSSOVER")
        cg.add(var.config_crossover_filter(crossover_config[CONF_FILTER]))
        cg.add(var.config_crossover_frequency(crossover_config[CONF_FREQUENCY]))
        cg.add(var.config_crossover_tweeter_channel(crossover_config[CONF_TWEETER_CHANNEL]))
        cg.add(var.config_crossover_dsp_sample_rate(crossover_config[CONF_DSP_SAMPLE_RATE]))
        cg.add(var.config_crossover_driver(ROLE_TWEETER, crossover_config[CONF_TWEETER_GAIN],
                                           crossover_config[CONF_TWEETER_INVERT], crossover_config[CONF_TWEETER_DELAY]))
        cg.add(var.config_crossover_driver(ROLE_MID, crossover_config[CONF_MID_GAIN],
                                           crossover_config[CONF_MID_INVERT], crossover_config[CONF_MID_DELAY]))
        for role, key in ((ROLE_TWEETER, CONF_TWEETER_PEQ), (ROLE_MID, CONF_MID_PEQ)):
            for band, peq in enumerate(crossover_config[key]):
                cg.add(var.config_crossover_peq(role, band, peq[CONF_FREQUENCY], peq[CONF_GAIN], peq[CONF_Q], peq[CONF_TYPE]))
        notch = crossover_config[CONF_TWEETER_NOTCH]
        cg.add(var.config_crossover_compensation(crossover_config[CONF_SUBSONIC_FREQUENCY], crossover_config[CONF_BAFFLE_WIDTH],
                                                 crossover_config[CONF_BAFFLE_PLACEMENT], notch[CONF_FREQUENCY],
                                                 notch[CONF_GAIN], notch[CONF_Q], crossover_config[CONF_AIR_GAIN]))
        cg.add(var.config_crossover_frequency_min(crossover_config[CONF_TWEETER_MIN_FREQUENCY]))
        linkwitz = crossover_config[CONF_LINKWITZ]
        cg.add(var.config_crossover_linkwitz(linkwitz[CONF_F0], linkwitz[CONF_Q0], linkwitz[CONF_FP], linkwitz[CONF_QP]))
        cg.add(var.config_crossover_headroom_compensation(crossover_config[CONF_HEADROOM_COMPENSATION]))
        cg.add(var.config_crossover_loudness(crossover_config[CONF_LOUDNESS], crossover_config[CONF_LOUDNESS_REFERENCE]))
