#pragma once

#include "../tas58xx.h"
#include "esphome/components/button/button.h"
#include "esphome/core/component.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

enum CrossoverButtonAction : uint8_t {
  XO_BUTTON_EXPORT = 0,
  XO_BUTTON_RESET,
};

class CrossoverButton : public button::Button, public Component, public Parented<Tas58xxComponent> {
 public:
  void dump_config() override;
  void set_action(CrossoverButtonAction action) { this->action_ = action; }

 protected:
  void press_action() override;

  CrossoverButtonAction action_{XO_BUTTON_EXPORT};
};

#endif  // USE_TAS58XX_EQ_CROSSOVER

}  // namespace esphome::tas58xx
