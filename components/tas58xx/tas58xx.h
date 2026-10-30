#pragma once

#include "esphome/components/audio_dac/audio_dac.h"
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/hal.h"

#include "tas58xx_defs.h"
#include "tas58xx_eq_common.h"
#include "tas58xx_eq_bands.h"
#include "tas58xx_eq_profiles.h"

#ifdef USE_TAS58XX_EQ_CROSSOVER
#include "tas58xx_crossover.h"
#endif

#ifdef USE_TAS58XX_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

namespace esphome::tas58xx {

class Tas58xxComponent : public audio_dac::AudioDac, public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;

  void loop() override;
  void update() override;

  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::IO; }

  void set_enable_pin(GPIOPin *enable) { this->enable_pin_ = enable; }

  // optional YAML config

  void config_analog_gain(float analog_gain) { this->tas58xx_analog_gain_ = analog_gain; }

  void config_dac_mode(DacMode dac_mode) {this->tas58xx_dac_mode_ = dac_mode; }

  void config_modulation_scheme(ModulationScheme modulation_scheme) {this->tas58xx_modulation_scheme_ = modulation_scheme; }

  void config_eq_mode(uint8_t configured_eq_mode) { this->configured_eq_mode_ = static_cast<EqMode>(configured_eq_mode); }

  void config_ignore_fault_mode(ExcludeIgnoreMode ignore_fault_mode) {
    this->ignore_clock_faults_when_clearing_faults_ = (ignore_fault_mode == ExcludeIgnoreMode::CLOCK_FAULT);
  }

  void config_mixer_mode(MixerMode mixer_mode) {this->tas58xx_mixer_mode_ = mixer_mode; }

  void config_refresh_eq(EqRefreshMode eq_refresh) { this->eq_refresh_ = eq_refresh; }

  // configured maximum and minimum with units dB
  void config_volume_max(float volume_max) { this->tas58xx_volume_max_ = (int8_t)(volume_max); }
  void config_volume_min(float volume_min) { this->tas58xx_volume_min_ = (int8_t)(volume_min); }

  #ifdef USE_TAS58XX_EQ_CROSSOVER
  // YAML 'crossover:' defaults, used until a crossover entity restores a saved value
  void config_crossover_filter(uint8_t filter) { this->xover_.filter = static_cast<crossover::CrossoverFilter>(filter); }
  void config_crossover_frequency(float frequency) { this->xover_.frequency = frequency; }
  void config_crossover_tweeter_channel(uint8_t channel) { this->xover_tweeter_channel_ = static_cast<Channels>(channel); }
  void config_crossover_dsp_sample_rate(float sample_rate) { this->xover_dsp_sample_rate_ = sample_rate; }
  void config_crossover_driver(uint8_t role, float gain_db, bool invert, float delay_mm) {
    this->xover_.driver[role].gain_db = gain_db;
    this->xover_.driver[role].invert = invert;
    this->xover_.driver[role].delay_mm = delay_mm;
  }
  void config_crossover_peq(uint8_t role, uint8_t band, float frequency, float gain_db, float q, uint8_t type) {
    this->xover_.driver[role].peq[band] = {frequency, gain_db, q, static_cast<crossover::PeqType>(type)};
  }
  void config_crossover_compensation(float subsonic_frequency, float baffle_width_cm, uint8_t baffle_placement,
                                     float notch_frequency, float notch_gain_db, float notch_q, float air_gain_db) {
    this->xover_.subsonic_frequency = subsonic_frequency;
    this->xover_.baffle_width_cm = baffle_width_cm;
    this->xover_.baffle_placement = static_cast<crossover::BafflePlacement>(baffle_placement);
    this->xover_.notch_frequency = notch_frequency;
    this->xover_.notch_gain_db = notch_gain_db;
    this->xover_.notch_q = notch_q;
    this->xover_.air_gain_db = air_gain_db;
  }
  void config_crossover_loudness(bool enabled, float reference_db) {
    this->xover_.loudness = enabled;
    this->xover_.loudness_reference_db = reference_db;
  }
  #endif


  #ifdef USE_TAS58XX_BINARY_SENSOR
  void config_exclude_fault(ExcludeIgnoreMode exclude_fault) {
    this->exclude_clock_fault_from_have_faults_ = (exclude_fault == ExcludeIgnoreMode::CLOCK_FAULT);
  }

  SUB_BINARY_SENSOR(have_fault)
  SUB_BINARY_SENSOR(left_channel_dc_fault)
  SUB_BINARY_SENSOR(right_channel_dc_fault)
  SUB_BINARY_SENSOR(left_channel_over_current_fault)
  SUB_BINARY_SENSOR(right_channel_over_current_fault)

  SUB_BINARY_SENSOR(otp_crc_check_error)
  SUB_BINARY_SENSOR(bq_write_failed_fault)
  SUB_BINARY_SENSOR(clock_fault)
  SUB_BINARY_SENSOR(pvdd_over_voltage_fault)
  SUB_BINARY_SENSOR(pvdd_under_voltage_fault)

  SUB_BINARY_SENSOR(over_temperature_shutdown_fault)
  SUB_BINARY_SENSOR(over_temperature_warning)
  #endif

  void enable_dac(bool enable);

  uint8_t get_configured_eq_mode();

  uint8_t get_mixer_mode();
  bool set_mixer_mode(MixerMode mode);

  bool is_eq_configured();

  void refresh_eq_settings();

  bool set_channel_volume(Channels channel, int8_t volume_dB);

  void select_eq_mode(uint8_t select_index);

  bool set_eq_gain(Channels channel, uint8_t band_index, int8_t gain);

  bool set_eq_preset(Channels channel, uint8_t select_preset);

  #ifdef USE_TAS58XX_EQ_CROSSOVER
  // used by crossover numbers, selects and switches
  // role is crossover::DriverRole and band is the PEQ band index where relevant
  float get_crossover_value(CrossoverParam param, uint8_t role, uint8_t band);
  void set_crossover_value(CrossoverParam param, uint8_t role, uint8_t band, float value);
  #endif

  bool is_muted() override { return this->is_muted_; }
  bool set_mute_off() override;
  bool set_mute_on() override;

  uint32_t times_faults_cleared();

  bool using_auto_eq_refresh();
  bool using_manual_eq_refresh();

  float volume() override;
  bool set_volume(float value) override;

 protected:
   GPIOPin* enable_pin_{nullptr};

   bool configure_registers_();

   bool get_analog_gain_(uint8_t* raw_gain);
   bool set_analog_gain_(float gain_db);

   bool get_dac_mode_(DacMode* mode);
   bool set_dac_mode_(DacMode mode);

   bool set_deep_sleep_off_();
   bool set_deep_sleep_on_();

   bool get_digital_volume_(uint8_t* raw_volume);
   bool set_digital_volume_(uint8_t new_volume);

   bool get_eq_mode_(EqMode* current_mode);
   bool set_eq_mode_(EqMode new_mode);

   bool set_modulation_scheme_(ModulationScheme modulation);

   bool get_state_(ControlState* state);
   bool set_state_(ControlState state);

   // mute bit for DEVICE_CTRL_2, combines user mute with the crossover safety mute
   uint8_t ctrl2_mute_bits_();

   bool write_eq_mode_register_(EqMode mode);

#if defined(USE_TAS58XX_CHANNEL_VOLUMES) || defined(USE_TAS58XX_EQ_CROSSOVER)
   bool write_channel_gain_(Channels channel);
#endif

#ifdef USE_TAS58XX_EQ_CROSSOVER
   // crossover state machine run from 'loop', returns true while busy
   bool crossover_loop_();
   void crossover_step_failed_(const char* what);
   void request_crossover_apply_(bool verify_only);
   bool wait_for_stable_clock_();
   bool set_safety_mute_(bool mute);

   bool tweeter_must_be_muted_();
   Channels crossover_channel_(crossover::DriverRole role);

   void encode_crossover_chain_(crossover::DriverRole role, uint8_t* data);
   bool write_crossover_chain_(crossover::DriverRole role);
   bool verify_crossover_chain_(crossover::DriverRole role);
   bool write_crossover_band_(crossover::DriverRole role, uint8_t band);
   void crossover_band_changed_(crossover::DriverRole role, uint8_t band);
   void crossover_sections_changed_();
   void update_loudness_();
#endif

   // manage faults
   bool clear_fault_registers_();

   #ifdef USE_TAS58XX_BINARY_SENSOR
   void publish_faults_();
   void publish_channel_faults_();
   void publish_global_faults_();
   #endif

   bool read_fault_registers_();


   // low level functions
   bool book_and_page_write_(uint8_t book, uint8_t page, uint8_t sub_addr, uint8_t* data, uint8_t number_bytes);

#ifdef USE_TAS58XX_EQ_CROSSOVER
   // write or read a block spanning several pages while holding the book selected
   bool book_and_page_block_(uint8_t book, uint8_t page, uint8_t sub_addr, uint8_t* data, uint16_t number_bytes, bool read);
#endif

  //  int32_t gain_to_f9_23_(int8_t gain);

   bool tas58xx_read_bytes_(uint8_t a_register, uint8_t* data, uint8_t number_bytes);
   bool tas58xx_write_byte_(uint8_t a_register, uint8_t data);
   bool tas58xx_write_bytes_(uint8_t a_register, uint8_t *data, uint8_t number_bytes);

   //// variables
   EqMode configured_eq_mode_; // derived from YAML

   enum ErrorCode {
     NONE = 0,
     CONFIGURATION_FAILED,
   } error_code_{NONE};

   // configured by YAML
   EqRefreshMode eq_refresh_;  // YAML default 'AUTO' = 0

   #ifdef USE_TAS58XX_BINARY_SENSOR
   bool exclude_clock_fault_from_have_faults_; // YAML default = true
   #endif

   bool ignore_clock_faults_when_clearing_faults_; // YAML default = true

   float tas58xx_analog_gain_; // configured in YAML

   uint8_t tas58xx_channel_preset_[NUMBER_CHANNELS]{0};
   int8_t tas58xx_channel_volume_[NUMBER_CHANNELS]{0};

   ControlState tas58xx_control_state_; // initialised in setup

   DacMode tas58xx_dac_mode_; // configured in YAML

#if defined(USE_TAS58XX_EQ_GAINS) || defined(USE_TAS58XX_EQ_PRESETS) || defined(USE_TAS58XX_EQ_CROSSOVER)
   bool eq_configured_{true};
#else
   bool eq_configured_{false};
#endif

   int8_t tas58xx_eq_gain_[NUMBER_CHANNELS][NUMBER_EQ_BANDS]{0}; // used if eq gain numbers are defined in YAML

   EqMode tas58xx_eq_mode_{EQ_OFF}; // current selected eq mode = EQ_OFF or EqMode configured_eq_mode_

   MixerMode tas58xx_mixer_mode_; // YAML default = STEREO

   ModulationScheme tas58xx_modulation_scheme_; // YAML default = BD Mode

   uint8_t tas58xx_raw_volume_max_; // maximum volume as digital volume register range 254 to 0
   uint8_t tas58xx_raw_volume_min_; // minimum volume as digital volume register range 254 to 0

   int8_t tas58xx_volume_max_;  // YAML configured maximum volume dB
   int8_t tas58xx_volume_min_;  // YAML configured maximum volume dB

   //// fault processing variables
   bool is_fault_to_clear_{false}; // false so clear fault registers is skipped on first update

   bool is_new_channel_fault_{true};  // conditionally publish binary sensors - initially true so published on first update
   bool is_new_common_fault_{true};
   bool is_new_global_fault_{true};
   bool is_new_over_temperature_issue_{true};

   Tas58xxFault tas58xx_faults_;  // current state of faults

   uint32_t times_faults_cleared_{0}; // counts number of times the faults register is cleared (used for publishing to sensor)

   //// utility variables used by loop, update and dump_config
   bool update_delay_finished_{false}; // use to indicate if delay before starting 'update' starting is complete

   uint8_t i2c_error_{0}; // last i2c error

   uint8_t loop_counter_{0}; // counts number of 'loop' iterations before proceeding

   LoopSetupStage loop_setup_stage_{WAIT_FOR_TRIGGER}; // used for state machine in 'loop'

   uint16_t number_registers_configured_{0}; // number tas58xx registers configured during 'setup'

   uint8_t refresh_band_{0}; // eq band currently being refreshed by 'loop'

   uint32_t start_time_; // initialised in setup, used for delay in starting 'update'

   bool safety_mute_{false}; // crossover holds the DAC muted until the crossover is written and verified

#ifdef USE_TAS58XX_EQ_CROSSOVER
   crossover::Settings xover_;                     // YAML defaults then entity values
   Channels xover_tweeter_channel_{LEFT_CHANNEL};  // YAML configured, other channel is the mid
   float xover_dsp_sample_rate_{96000.0f};         // DSP processing rate set by register 0x46 in tas58xx_minimal.h
   float xover_volume_db_{0.0f};                   // last digital volume from 'set_volume', drives loudness
   crossover::LoudnessBoost xover_loudness_applied_;

   CrossoverStep xover_step_{XO_IDLE};
   bool xover_pending_{false};        // a full apply has been requested
   bool xover_verify_only_{false};    // pending request only needs a readback check
   bool xover_applied_{false};        // crossover written and verified, tweeter may play
   uint8_t xover_clock_good_reads_{0};
   uint8_t xover_wait_loops_{0};
   uint16_t xover_failures_{0};
   uint32_t xover_last_wait_log_{0};
#endif
};

}  // namespace esphome::tas58xx
