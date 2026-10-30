#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "../tas58xx.h"

namespace esphome::tas58xx {

class FaultSensor : public PollingComponent, public Parented<Tas58xxComponent> {
 public:
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_times_faults_cleared_sensor(sensor::Sensor* sensor) { times_faults_cleared_sensor_ = sensor; }

#ifdef USE_TAS58XX_EQ_CROSSOVER
  void set_peak_gain_sensor(uint8_t role, sensor::Sensor* sensor) { peak_gain_sensor_[role] = sensor; }
#endif

 protected:
  sensor::Sensor* times_faults_cleared_sensor_{nullptr};

  // initialise as large number so first value of first update interval is saved
  uint32_t last_faults_cleared_{100000};

#ifdef USE_TAS58XX_EQ_CROSSOVER
  sensor::Sensor* peak_gain_sensor_[2]{nullptr, nullptr};  // tweeter, mid
  float last_peak_gain_[2]{-1000.0f, -1000.0f};
#endif
};

}  // namespace esphome::tas58xx
