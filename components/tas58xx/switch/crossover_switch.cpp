#include "crossover_switch.h"
#include "esphome/core/log.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

static constexpr const char* TAG = "tas58xx.switch";

void CrossoverSwitch::setup() {
  bool state = false;
  if (this->param_ != XO_PARAM_MUTE) {
    // saved state if there is one, otherwise the value from YAML 'crossover:'
    // driver mutes are for measuring and always start off
    optional<bool> restored = this->get_initial_state();
    state = restored.has_value() ? restored.value()
                                 : (this->parent_->get_crossover_value(this->param_, this->role_, 0) >= 0.5f);
  }
  this->write_state(state);

  // after a reset to YAML values, show and save the new value
  this->parent_->add_crossover_listener([this]() {
    this->publish_state(this->parent_->get_crossover_value(this->param_, this->role_, 0) >= 0.5f);
  });
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
