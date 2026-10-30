#include "crossover_switch.h"
#include "esphome/core/log.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

static constexpr const char* TAG = "tas58xx.switch";

void CrossoverSwitch::setup() {
  // saved state if there is one, otherwise the value from YAML 'crossover:'
  optional<bool> restored = this->get_initial_state();
  bool state = restored.has_value() ? restored.value()
                                    : (this->parent_->get_crossover_value(this->param_, this->role_, 0) >= 0.5f);
  this->write_state(state);
}

void CrossoverSwitch::dump_config() {
  LOG_SWITCH("", "Tas58xx Crossover Switch", this);
}

void CrossoverSwitch::write_state(bool state) {
  this->publish_state(state);
  this->parent_->set_crossover_value(this->param_, this->role_, 0, state ? 1.0f : 0.0f);
}

#endif  // USE_TAS58XX_EQ_CROSSOVER

}  // namespace esphome::tas58xx
