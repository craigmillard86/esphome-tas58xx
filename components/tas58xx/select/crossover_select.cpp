#include "crossover_select.h"
#include "esphome/core/log.h"

namespace esphome::tas58xx {

#ifdef USE_TAS58XX_EQ_CROSSOVER

static constexpr const char* TAG = "tas58xx.select";

void CrossoverSelect::setup() {
  size_t restored_index;
  this->pref_ = this->make_entity_preference<size_t>();
  // first boot uses the value from YAML 'crossover:'
  if (!this->pref_.load(&restored_index) || restored_index >= this->size()) {
    restored_index = static_cast<size_t>(this->parent_->get_crossover_value(this->param_, this->role_, this->band_));
  }
  this->publish_state(restored_index);
  this->parent_->set_crossover_value(this->param_, this->role_, this->band_, static_cast<float>(restored_index));

  // after a reset to YAML values, show and save the new value
  this->parent_->add_crossover_listener([this]() {
    size_t current = static_cast<size_t>(this->parent_->get_crossover_value(this->param_, this->role_, this->band_));
    this->publish_state(current);
    this->pref_.save(&current);
  });
}

void CrossoverSelect::dump_config() {
  LOG_SELECT("", "Tas58xx Crossover Select", this);
}

void CrossoverSelect::control(size_t index) {
  this->publish_state(index);
  this->pref_.save(&index);
  this->parent_->set_crossover_value(this->param_, this->role_, this->band_, static_cast<float>(index));
}

#endif  // USE_TAS58XX_EQ_CROSSOVER

}  // namespace esphome::tas58xx
