#pragma once

#include "../tas58xx.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

// one class for every crossover switch, the parameter decides what it controls
class CrossoverSwitch : public switch_::Switch, public Component, public Parented<Tas58xxComponent> {
 public:
  void setup() override;
  void dump_config() override;
  // restore before the DAC component first writes the crossover in 'loop'
  float get_setup_priority() const override { return setup_priority::IO - 1.0f; }

  void set_crossover_param(CrossoverParam param, uint8_t role) {
    this->param_ = param;
    this->role_ = role;
  }

 protected:
  void write_state(bool state) override;

  CrossoverParam param_{XO_PARAM_LOUDNESS};
  uint8_t role_{0};
};

#endif  // USE_TAS58XX_EQ_CROSSOVER

}  // namespace esphome::tas58xx
