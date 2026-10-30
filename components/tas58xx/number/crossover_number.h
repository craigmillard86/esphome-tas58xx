#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "../tas58xx.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

// one class for every crossover number, the parameter decides what it controls
class CrossoverNumber : public number::Number, public Component, public Parented<Tas58xxComponent> {
 public:
  void setup() override;
  void dump_config() override;
  // restore before the DAC component first writes the crossover in 'loop'
  float get_setup_priority() const override { return setup_priority::IO - 1.0f; }

  void set_crossover_param(CrossoverParam param, uint8_t role, uint8_t band) {
    this->param_ = param;
    this->role_ = role;
    this->band_ = band;
  }

 protected:
  void control(float value) override;

  ESPPreferenceObject pref_;

  CrossoverParam param_{XO_PARAM_FREQUENCY};
  uint8_t role_{0};
  uint8_t band_{0};
};

#endif  // USE_TAS58XX_EQ_CROSSOVER

}  // namespace esphome::tas58xx
