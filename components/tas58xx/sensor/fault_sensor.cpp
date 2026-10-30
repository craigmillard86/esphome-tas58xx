#include "esphome/core/log.h"
#include "fault_sensor.h"

namespace esphome::tas58xx {

static constexpr const char* TAG = "tas58xx.sensor";

void  FaultSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "Tas58xx Sensor:");
  LOG_SENSOR("  ", "Times Faults Cleared", this->times_faults_cleared_sensor_);
#ifdef USE_TAS58XX_EQ_CROSSOVER
  LOG_SENSOR("  ", "Tweeter Peak Gain", this->peak_gain_sensor_[0]);
  LOG_SENSOR("  ", "Mid Peak Gain", this->peak_gain_sensor_[1]);
#endif
  LOG_UPDATE_INTERVAL(this);
}

void  FaultSensor::update() {
  if (this->times_faults_cleared_sensor_ != nullptr) {
    // only publish if different to last value but will publish first value
    uint32_t current_faults_cleared = this->parent_->times_faults_cleared();
    if (current_faults_cleared != this->last_faults_cleared_) {
      this->times_faults_cleared_sensor_->publish_state(current_faults_cleared);
      this->last_faults_cleared_ = current_faults_cleared;
    }
  }

#ifdef USE_TAS58XX_EQ_CROSSOVER
  static constexpr const char* PEAK_NAME[2] = {"Tweeter", "Mid"};
  for (uint8_t role = 0; role < 2; role++) {
    if (this->peak_gain_sensor_[role] == nullptr) continue;
    const float peak = roundf(this->parent_->get_crossover_peak_db(role) * 10.0f) / 10.0f;
    if (peak == this->last_peak_gain_[role]) continue;
    // above 0dB a full scale signal can clip inside the DSP
    if (peak > 0.0f) ESP_LOGW(TAG, "%s DSP chain peaks at +%.1fdB, loud passages may clip", PEAK_NAME[role], peak);
    this->peak_gain_sensor_[role]->publish_state(peak);
    this->last_peak_gain_[role] = peak;
  }
#endif
}

}  // namespace esphome::tas58xx
