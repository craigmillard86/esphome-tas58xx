import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_SOUND_PRESSURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL,
)

CONF_FAULTS_CLEARED = "faults_cleared"
CONF_TWEETER_PEAK_GAIN = "tweeter_peak_gain"
CONF_MID_PEAK_GAIN = "mid_peak_gain"

from ..audio_dac import CONF_TAS58XX_ID, ROLE_MID, ROLE_TWEETER, Tas58xxComponent, tas58xx_ns, validate_crossover_entities

FaultSensor = tas58xx_ns.class_("FaultSensor", cg.PollingComponent)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FaultSensor),
            cv.GenerateID(CONF_TAS58XX_ID): cv.use_id(Tas58xxComponent),

            cv.Optional(CONF_FAULTS_CLEARED): sensor.sensor_schema(
                    accuracy_decimals=0,
                    state_class=STATE_CLASS_MEASUREMENT,
            ),

            # crossover: highest gain of each driver's DSP chain, above 0dB loud passages can clip
            cv.Optional(CONF_TWEETER_PEAK_GAIN): sensor.sensor_schema(
                    unit_of_measurement=UNIT_DECIBEL,
                    accuracy_decimals=1,
                    device_class=DEVICE_CLASS_SOUND_PRESSURE,
                    state_class=STATE_CLASS_MEASUREMENT,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                    icon="mdi:chart-bell-curve",
            ),
            cv.Optional(CONF_MID_PEAK_GAIN): sensor.sensor_schema(
                    unit_of_measurement=UNIT_DECIBEL,
                    accuracy_decimals=1,
                    device_class=DEVICE_CLASS_SOUND_PRESSURE,
                    state_class=STATE_CLASS_MEASUREMENT,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                    icon="mdi:chart-bell-curve",
            ),
        }
    ).extend(cv.polling_component_schema("60s"))
)

def _final_validate(config):
    validate_crossover_entities(config, [CONF_TWEETER_PEAK_GAIN, CONF_MID_PEAK_GAIN])
    return config

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):
    tas58xx_component = await cg.get_variable(config[CONF_TAS58XX_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cg.register_parented(var, tas58xx_component)

    if clear_faults_config := config.get(CONF_FAULTS_CLEARED):
      sens = await sensor.new_sensor(clear_faults_config)
      cg.add(var.set_times_faults_cleared_sensor(sens))

    for key, role in ((CONF_TWEETER_PEAK_GAIN, ROLE_TWEETER), (CONF_MID_PEAK_GAIN, ROLE_MID)):
      if peak_config := config.get(key):
        sens = await sensor.new_sensor(peak_config)
        cg.add(var.set_peak_gain_sensor(role, sens))
