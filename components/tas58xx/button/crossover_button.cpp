#include "crossover_button.h"
#include "esphome/core/log.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

static constexpr const char* TAG = "tas58xx.button";

void CrossoverButton::dump_config() {
  LOG_BUTTON("", "Tas58xx Crossover Button", this);
}

void CrossoverButton::press_action() {
  switch (this->action_) {
    case XO_BUTTON_EXPORT:
      this->parent_->export_crossover_settings();
      break;
    case XO_BUTTON_RESET:
      this->parent_->reset_crossover_settings();
      break;
  }
}

#endif  // USE_TAS58XX_EQ_CROSSOVER

}  // namespace esphome::tas58xx
