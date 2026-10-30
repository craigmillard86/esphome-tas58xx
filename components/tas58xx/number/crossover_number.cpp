#include "crossover_number.h"
#include "esphome/core/log.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

static constexpr const char* TAG = "tas58xx.number";

void CrossoverNumber::setup() {
  float value;
  this->pref_ = this->make_entity_preference<float>();
  // first boot uses the value from YAML 'crossover:'
  if (!this->pref_.load(&value)) value = this->parent_->get_crossover_value(this->param_, this->role_, this->band_);
  this->publish_state(value);
  this->parent_->set_crossover_value(this->param_, this->role_, this->band_, value);
}

void CrossoverNumber::dump_config() {
  LOG_NUMBER("", "Tas58xx Crossover Number", this);
}

void CrossoverNumber::control(float value) {
  this->publish_state(value);
  this->parent_->set_crossover_value(this->param_, this->role_, this->band_, value);
  this->pref_.save(&value);
}

#endif  // USE_TAS58XX_EQ_CROSSOVER

}  // namespace esphome::tas58xx
