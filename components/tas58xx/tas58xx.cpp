#include "tas58xx.h"
#include "tas58xx_minimal.h"
#include "tas58xx_helpers.h"

#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include <cstring>

namespace esphome::tas58xx {

#ifdef USE_TAS5805M_DAC
static constexpr const char* TAG = "tas5805m";
#else
static constexpr const char* TAG = "tas5825m";
#endif

static constexpr const char* ERROR = "Error";
static constexpr const char* MIXER_MODE = "Mixer Mode";
static constexpr const char* EQ_BAND = "EQ Band";

static constexpr uint8_t TAS58XX_MUTE_CONTROL = 0x08; // bit mask for mute control

static constexpr uint8_t DELAY_LOOPS = 40;  // 40 loop iterations ~ 300ms initial delay in 'loop' before writing eq settings

static constexpr uint16_t INITIAL_UPDATE_DELAY = 4000;  // initial ms delay before starting fault updates

#ifdef USE_TAS58XX_EQ_CROSSOVER
static constexpr const char* CROSSOVER = "Crossover";
static constexpr const char* ROLE_TEXT[crossover::NUMBER_ROLES] = {"Tweeter", "Mid"};
static constexpr const char* FILTER_TEXT[crossover::NUMBER_FILTERS] = {"LR2", "LR4", "BW2", "BW4"};

static constexpr uint8_t CLOCK_STABLE_READS = 4;       // consecutive valid FS_MON reads before writing coefficients
static constexpr uint8_t CLOCK_READ_INTERVAL = 2;      // loop iterations between FS_MON reads while clock is valid
static constexpr uint8_t CLOCK_POLL_INTERVAL = 10;     // loop iterations between FS_MON reads while waiting for a clock
static constexpr uint8_t RETRY_INTERVAL = 60;          // loop iterations before retrying after a failed write or verify
static constexpr uint32_t WAIT_LOG_INTERVAL = 10000;   // ms between 'waiting for clock' log messages

static constexpr uint16_t CHAIN_BYTES = crossover::NUMBER_BANDS * BIQUAD_SIZE;
static_assert(BIQUAD_SIZE == biquad::ENCODED_SIZE, "biquad size mismatch");

static const AddressSequence* eq_address(Channels channel, uint8_t band) {
#ifdef USE_TAS5805M_DAC
  return (channel == LEFT_CHANNEL) ? &TAS5805M_LEFT_EQ_ADDRESS[band] : &TAS5805M_RIGHT_EQ_ADDRESS[band];
#else
  return (channel == LEFT_CHANNEL) ? &TAS5825M_LEFT_EQ_ADDRESS[band] : &TAS5825M_RIGHT_EQ_ADDRESS[band];
#endif
}
#endif

void Tas58xxComponent::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");

#ifdef USE_TAS58XX_EQ_CROSSOVER
  // never let the tweeter play full range: stay muted until the crossover has been written and verified
  this->safety_mute_ = true;
  // crossover is on by default, an EQ Mode select can still switch it Off
  this->tas58xx_eq_mode_ = this->configured_eq_mode_;
  // crossover entities set up after this component, so these are still the YAML values
  this->xover_defaults_ = this->xover_;
  this->xover_default_tweeter_channel_ = this->xover_tweeter_channel_;
#endif
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();
    this->enable_pin_->digital_write(false);
    delay(1);
    this->enable_pin_->digital_write(true);
    delay(5);
  }

  if (!this->configure_registers_()) {
    this->error_code_ = CONFIGURATION_FAILED;
    this->mark_failed();
  }

  // rescale -103db to 24db digital volume range to register digital volume range 254 to 0
  this->tas58xx_raw_volume_max_ = (uint8_t)((this->tas58xx_volume_max_ - 24) * -2);
  this->tas58xx_raw_volume_min_ = (uint8_t)((this->tas58xx_volume_min_ - 24) * -2);

#ifdef USE_TAS58XX_EQ_CROSSOVER
  // crossover writes are gated on the DAC reporting a valid I2S clock, so an early trigger is safe
  // with refresh_eq: MANUAL the EQ Mode select triggers the refresh instead
  if (this->using_auto_eq_refresh()) {
    this->refresh_eq_settings();
    ESP_LOGD(TAG, "%s setup triggered EQ settings refresh", CROSSOVER);
  }
#endif
}

bool Tas58xxComponent::configure_registers_() {
  static constexpr uint8_t ESPHOME_MAXIMUM_DELAY = 5; // milliseconds

  uint16_t i = 0;
  uint16_t counter = 0;
  uint16_t number_configurations = sizeof(TAS58XX_CONFIG) / sizeof(TAS58XX_CONFIG[0]);

  while (i < number_configurations) {
    switch (TAS58XX_CONFIG[i].addr) {
      case TAS58XX_CFG_META_DELAY:
        if (TAS58XX_CONFIG[i].value > ESPHOME_MAXIMUM_DELAY) return false;
        delay(TAS58XX_CONFIG[i].value);
        break;
      default:
        if (!this->tas58xx_write_byte_(TAS58XX_CONFIG[i].addr, TAS58XX_CONFIG[i].value)) return false;
        counter++;
        break;
    }
    i++;
  }
  this->number_registers_configured_ = counter;

  // enable Tas58xx
  if (!this->set_deep_sleep_off_()) return false;

  if (!this->set_modulation_scheme_(this->tas58xx_modulation_scheme_)) return false;

  if (!this->set_dac_mode_(this->tas58xx_dac_mode_)) return false;

  if (!this->set_analog_gain_(this->tas58xx_analog_gain_)) return false;

  if (!this->set_state_(CTRL_PLAY)) return false;

  this->start_time_ = App.get_loop_component_start_time();
  return true;
}

void Tas58xxComponent::loop() {
  // 'play_file' is initiated by YAML on_boot with priority 220.0f
  // 'refresh_eq_settings' is triggered by Number 'left_eq_gain_16000hz' or 'right_eq_gain_16000hz' or Select 'eq_mode'
  // each with setup priority AFTER_CONNECTION = 100.0f
  // delay refreshing EQ settings until refresh is triggered so tas58xx has detected i2s clock through sound being played

  // loop_setup_stage_ is initially WAIT_FOR_TRIGGER

  switch (this->loop_setup_stage_) {
    case WAIT_FOR_TRIGGER:
      return;

    case RUN_DELAY_LOOP:
      if (this->loop_counter_ < DELAY_LOOPS) {    // loop_count was initialised to 0
        this->loop_counter_++;
        return;
      }
#ifdef USE_TAS58XX_EQ_CROSSOVER
      this->loop_setup_stage_ = WAIT_FOR_CLOCK;
#else
      this->loop_setup_stage_ = INPUT_MIXER_SETUP;
#endif
      return;

    case WAIT_FOR_CLOCK:
#ifdef USE_TAS58XX_EQ_CROSSOVER
      if (!this->wait_for_stable_clock_()) return;
#endif
      this->loop_setup_stage_ = INPUT_MIXER_SETUP;
      return;

    case INPUT_MIXER_SETUP:
      // setup Eq Mode first
      if (!this->set_eq_mode_(this->tas58xx_eq_mode_)) {
         ESP_LOGW(TAG, "%s setting EQ Mode: %s", ERROR, EQ_MODE_TEXT[this->tas58xx_eq_mode_]);
      }

      if (!this->set_mixer_mode(this->tas58xx_mixer_mode_)) {
        ESP_LOGW(TAG, "%s setting %s: %s", ERROR, MIXER_MODE, MIXER_MODE_TEXT[this->tas58xx_mixer_mode_]);
      }
      this->loop_setup_stage_ = LR_VOLUME_SETUP;
      return;

    case LR_VOLUME_SETUP:
#ifdef USE_TAS58XX_CHANNEL_VOLUMES
      if (!this->set_channel_volume(LEFT_CHANNEL, this->tas58xx_channel_volume_[LEFT_CHANNEL])) {
        ESP_LOGW(TAG, "%s setting Left Channel Gain: %ddb", ERROR, this->tas58xx_channel_volume_[LEFT_CHANNEL]);
      }

      if (!this->set_channel_volume(RIGHT_CHANNEL, this->tas58xx_channel_volume_[RIGHT_CHANNEL])) {
        ESP_LOGW(TAG, "%s setting Right Channel Gain: %ddb", ERROR, this->tas58xx_channel_volume_[RIGHT_CHANNEL]);
      }
#endif

#ifdef USE_TAS58XX_EQ_GAINS
      this->loop_setup_stage_ = EQ_BANDS_SETUP;
#endif

#ifdef USE_TAS58XX_EQ_PRESETS
      this->loop_setup_stage_ = EQ_PRESETS_SETUP;
#endif

#ifdef USE_TAS58XX_EQ_CROSSOVER
      this->loop_setup_stage_ = CROSSOVER_SETUP;
      this->request_crossover_apply_(false);
#endif

      // if loop_setup_stage_ has not changed then no EQ configured
      if (this->loop_setup_stage_ == LR_VOLUME_SETUP) this->loop_setup_stage_ = SETUP_COMPLETE;
      return;

    case EQ_BANDS_SETUP:
#ifdef USE_TAS58XX_EQ_GAINS
      if (this->refresh_band_ == NUMBER_EQ_BANDS) {     // refresh_band_ was initialised to 0
        // finished writing all bands
        this->loop_setup_stage_ = SETUP_COMPLETE;
        this->refresh_band_ = 0;
        return;
      }

      if (!this->set_eq_gain(LEFT_CHANNEL, this->refresh_band_, this->tas58xx_eq_gain_[LEFT_CHANNEL][this->refresh_band_])) {
#ifdef USE_TAS58XX_EQ_BIAMP
        ESP_LOGW(TAG, "%s setting Gain Left %s: %d", ERROR, EQ_BAND, this->refresh_band_ + 1);
#else
        ESP_LOGW(TAG, "%s setting Gain %s: %d", ERROR, EQ_BAND, this->refresh_band_ + 1);
#endif
      }

#ifdef USE_TAS58XX_EQ_BIAMP
      if (!this->set_eq_gain(RIGHT_CHANNEL, this->refresh_band_, this->tas58xx_eq_gain_[RIGHT_CHANNEL][this->refresh_band_])) {
        ESP_LOGW(TAG, "%s setting Gain Right %s: %d", ERROR, EQ_BAND, this->refresh_band_ + 1);
      }
#endif

      this->refresh_band_++;
#endif // USE_TAS58XX_EQ_GAINS
      return;

    case EQ_PRESETS_SETUP:
#ifdef USE_TAS58XX_EQ_PRESETS
      if (!this->set_eq_preset(LEFT_CHANNEL, this->tas58xx_channel_preset_[LEFT_CHANNEL])) {
        ESP_LOGW(TAG, "%s setting Left Channel Preset index: %d", ERROR, this->tas58xx_channel_preset_[LEFT_CHANNEL]);
      }
      if (!this->set_eq_preset(RIGHT_CHANNEL, this->tas58xx_channel_preset_[RIGHT_CHANNEL])) {
        ESP_LOGW(TAG, "%s setting Right Channel Preset index: %d", ERROR, this->tas58xx_channel_preset_[RIGHT_CHANNEL]);
      }
      this->loop_setup_stage_ = SETUP_COMPLETE;
#endif
      return;

    case CROSSOVER_SETUP:
#ifdef USE_TAS58XX_EQ_CROSSOVER
      if (this->crossover_loop_()) return;
#endif
      this->loop_setup_stage_ = SETUP_COMPLETE;
      return;

    case SETUP_COMPLETE:
#ifdef USE_TAS58XX_EQ_CROSSOVER
      // keep looping while a crossover change is being applied, 'request_crossover_apply_' re-enables loop
      if (this->crossover_loop_()) return;
#endif
      this->disable_loop(); // requires Esphome 2025.7.0 or greater
      return;
  }
}

void Tas58xxComponent::update() {
  // initial delay before proceeding with updates
  if (!this->update_delay_finished_) {
    const uint32_t current_time = App.get_loop_component_start_time();
    this->update_delay_finished_ = ((current_time - this->start_time_) > INITIAL_UPDATE_DELAY);

    if (!this->update_delay_finished_) return;

    // finished delay so clear faults
    if (!this->tas58xx_write_byte_(TAS58XX_FAULT_CLEAR, TAS58XX_ANALOG_FAULT_CLEAR)) {
      ESP_LOGW(TAG, "%s initialising faults", ERROR);
    }

    // publish all binary sensors as false on first update
#ifdef USE_TAS58XX_BINARY_SENSOR
    this->publish_faults_();
#endif

    // read and process faults from next update
    return;
  }

  // after delay updates starts here

  // if there was a fault last update then clear any faults
  if (this->is_fault_to_clear_) {
    if (!this->clear_fault_registers_()) {
      ESP_LOGW(TAG, "%s clearing faults", ERROR);
    }
  }

  if (!this->read_fault_registers_()) {
    ESP_LOGW(TAG, "%s reading faults", ERROR);
    return;
  }

  // is there a fault that should be cleared next update
  this->is_fault_to_clear_ =
     ( this->tas58xx_faults_.is_fault_except_clock_fault || (this->tas58xx_faults_.clock_fault && (!this->ignore_clock_faults_when_clearing_faults_)) );


  // if no change in faults bypass publishing
  if ( !(this->is_new_common_fault_ || this->is_new_over_temperature_issue_ || this->is_new_channel_fault_ || this->is_new_global_fault_) ) return;

#ifdef USE_TAS58XX_BINARY_SENSOR
  this->publish_faults_();
#endif
}

void Tas58xxComponent::dump_config() {
#ifdef USE_TAS5805M_DAC
  ESP_LOGCONFIG(TAG, "Tas5805m Audio Dac:");
#else
  ESP_LOGCONFIG(TAG, "Tas5825m Audio Dac:");
#endif

  switch (this->error_code_) {
    case CONFIGURATION_FAILED:
      ESP_LOGE(TAG, "  %s setup failed: %i", ERROR, this->i2c_error_);
      break;
    case NONE:
      LOG_I2C_DEVICE(this);
      LOG_PIN("  Enable Pin: ", this->enable_pin_);
      ESP_LOGCONFIG(TAG,
              "  Registers Configured: %i\n"
              "  Analog Gain: %3.1fdB\n"
              "  Modulation: %s\n"
              "  DAC Mode: %s\n"
              "  Mixer Mode: %s\n"
              "  Volume Maximum: %idB\n"
              "  Volume Minimum: %idB\n"
              "  Ignore Fault: %s\n"
              "  Refresh EQ: %s\n",
              this->number_registers_configured_, this->tas58xx_analog_gain_,
              this->tas58xx_modulation_scheme_ ? "1SPW Mode" : "BD Mode",
              this->tas58xx_dac_mode_ ? "PBTL" : "BTL",
              MIXER_MODE_TEXT[this->tas58xx_mixer_mode_],
              this->tas58xx_volume_max_, this->tas58xx_volume_min_,
              this->ignore_clock_faults_when_clearing_faults_ ? "CLOCK FAULTS" : "NONE",
              this->eq_refresh_ ? "MANUAL" : "AUTO"
              );
#ifdef USE_TAS58XX_EQ_CROSSOVER
      ESP_LOGCONFIG(TAG,
              "  %s: %s %.0fHz\n"
              "    Tweeter Channel: %s\n"
              "    Tweeter Gain: %.1fdB  Mid Gain: %.1fdB\n"
              "    DSP Sample Rate: %.0fHz\n"
              "    Loudness: %s  Reference: %.1fdB",
              CROSSOVER, FILTER_TEXT[this->xover_.filter], crossover::effective_frequency(this->xover_),
              LR_CHANNEL_TEXT[this->xover_tweeter_channel_],
              this->xover_.driver[crossover::ROLE_TWEETER].gain_db, this->xover_.driver[crossover::ROLE_MID].gain_db,
              this->xover_dsp_sample_rate_,
              ONOFF(this->xover_.loudness), this->xover_.loudness_reference_db);
#endif
      LOG_UPDATE_INTERVAL(this);
      break;
  }

#ifdef USE_TAS58XX_BINARY_SENSOR
  ESP_LOGCONFIG(TAG, "Tas58xx Binary Sensors:");
  LOG_BINARY_SENSOR("  ", "Any Faults", this->have_fault_binary_sensor_);
  ESP_LOGCONFIG(TAG, "    Exclude: %s", this->exclude_clock_fault_from_have_faults_ ? "CLOCK FAULTS" : "NONE");

  LOG_BINARY_SENSOR("  ", "Right Channel Over Current", this->right_channel_over_current_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Left Channel Over Current", this->left_channel_over_current_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Right Channel DC Fault", this->right_channel_dc_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Left Channel DC Fault", this->left_channel_dc_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "PVDD Under Voltage", this->pvdd_under_voltage_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "PVDD Over Voltage", this->pvdd_over_voltage_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Clock Fault", this->clock_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BQ Write Failed", this->bq_write_failed_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OTP CRC Check Error", this->otp_crc_check_error_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Over Temperature Shutdown", this->over_temperature_shutdown_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Over Temperature Warning", this->over_temperature_warning_binary_sensor_);
#endif
}

// public //

// used by 'enable_dac_switch'
void Tas58xxComponent::enable_dac(bool enable) {
#ifdef USE_TAS58XX_EQ_CROSSOVER
  // wake muted so the tweeter stays silent until the crossover has been re-verified
  if (enable && this->loop_setup_stage_ >= CROSSOVER_SETUP &&
      this->tas58xx_control_state_ == CTRL_DEEP_SLEEP) this->safety_mute_ = true;
#endif
  enable ? this->set_deep_sleep_off_() : this->set_deep_sleep_on_();

#ifdef USE_TAS58XX_EQ_CROSSOVER
  // after deep sleep check the crossover is still in place before the tweeter plays
  if (enable && this->loop_setup_stage_ >= CROSSOVER_SETUP) this->request_crossover_apply_(true);
#endif
}

// used by select eq mode
uint8_t Tas58xxComponent::get_configured_eq_mode() {
  return static_cast<uint8_t>(this->configured_eq_mode_);
}

uint8_t Tas58xxComponent::get_mixer_mode() {
  return static_cast<uint8_t>(this->tas58xx_mixer_mode_);
}

bool Tas58xxComponent::set_mixer_mode(MixerMode mode) {

  this->tas58xx_mixer_mode_ = mode;

  // only save until ready to setup in 'loop'
  if (this->loop_setup_stage_ < INPUT_MIXER_SETUP) {
     ESP_LOGV(TAG, "Save %s: %s", MIXER_MODE, MIXER_MODE_TEXT[mode]);
     return true;
  }

  // follows order of input mixer registers = Left to Left, Right to Left, Left to Right, Right to Right
  struct MixerCoefficients {
    uint32_t l_to_l;
    uint32_t r_to_l;
    uint32_t l_to_r;
    uint32_t r_to_r;
  }__attribute__((packed));

  MixerCoefficients mixer_coefficients;

  switch (mode) {
    case STEREO:
      mixer_coefficients.l_to_l =TAS58XX_MIXER_COEFF_0DB;
      mixer_coefficients.r_to_l =TAS58XX_MIXER_COEFF_MUTE;
      mixer_coefficients.l_to_r =TAS58XX_MIXER_COEFF_MUTE;
      mixer_coefficients.r_to_r =TAS58XX_MIXER_COEFF_0DB;
      break;

    case STEREO_INVERSE:
      mixer_coefficients.l_to_l =TAS58XX_MIXER_COEFF_MUTE;
      mixer_coefficients.r_to_l =TAS58XX_MIXER_COEFF_0DB;
      mixer_coefficients.l_to_r =TAS58XX_MIXER_COEFF_0DB;
      mixer_coefficients.r_to_r =TAS58XX_MIXER_COEFF_MUTE;
      break;

    case MONO:
      mixer_coefficients.l_to_l =TAS58XX_MIXER_COEFF_MINUS6DB;
      mixer_coefficients.r_to_l =TAS58XX_MIXER_COEFF_MINUS6DB;
      mixer_coefficients.l_to_r =TAS58XX_MIXER_COEFF_MINUS6DB;
      mixer_coefficients.r_to_r =TAS58XX_MIXER_COEFF_MINUS6DB;
      break;

    case LEFT:
      mixer_coefficients.l_to_l =TAS58XX_MIXER_COEFF_0DB;
      mixer_coefficients.r_to_l =TAS58XX_MIXER_COEFF_MUTE;
      mixer_coefficients.l_to_r =TAS58XX_MIXER_COEFF_0DB;
      mixer_coefficients.r_to_r =TAS58XX_MIXER_COEFF_MUTE;
      break;

    case RIGHT:
      mixer_coefficients.l_to_l =TAS58XX_MIXER_COEFF_MUTE;
      mixer_coefficients.r_to_l =TAS58XX_MIXER_COEFF_0DB;
      mixer_coefficients.l_to_r =TAS58XX_MIXER_COEFF_MUTE;
      mixer_coefficients.r_to_r =TAS58XX_MIXER_COEFF_0DB;
      break;

    default:
      ESP_LOGE(TAG, "Invalid %s", MIXER_MODE);
      return false;
  }

  if (!this->book_and_page_write_(TAS58XX_AUDIO_CTRL_BOOK, TAS58XX_MIXER_GAIN_PAGE, TAS58XX_MIXER_GAIN_SUBADDR,
                                  reinterpret_cast<uint8_t*>(&mixer_coefficients), sizeof(MixerCoefficients))) {
    ESP_LOGW(TAG, "%s writing %s: %s", ERROR, MIXER_MODE, MIXER_MODE_TEXT[mode]);
    return false;
  }

  ESP_LOGD(TAG, "Set %s: %s", MIXER_MODE, MIXER_MODE_TEXT[mode]);
  return true;
}

// used by 'select eq mode' to determine initially selected EQ mode
bool Tas58xxComponent::is_eq_configured() {
  return this->eq_configured_;
}

// used by 'left_gain_band16000hz' or 'right_gain_band16000hz' or 'select eq_mode'
// to trigger loop setup
void Tas58xxComponent::refresh_eq_settings() {
  if (this->loop_setup_stage_ == WAIT_FOR_TRIGGER) {
    this->loop_setup_stage_ = RUN_DELAY_LOOP;
  }
  return;
}

bool Tas58xxComponent::set_channel_volume(Channels channel, int8_t volume_dB) {
#ifdef USE_TAS58XX_CHANNEL_VOLUMES
  if (volume_dB < TAS58XX_CHANNEL_VOLUME_MIN_DB || volume_dB > TAS58XX_CHANNEL_VOLUME_MAX_DB) {
    ESP_LOGE(TAG, "Invalid %s Channel Volume: %ddB", LR_CHANNEL_TEXT[channel], volume_dB);
    return false;
  }

  this->tas58xx_channel_volume_[channel] = volume_dB;

  // only save until ready to setup in 'loop'
  if (this->loop_setup_stage_ < LR_VOLUME_SETUP) {
    ESP_LOGV(TAG, "Save %s Channel Volume: %ddB", LR_CHANNEL_TEXT[channel], volume_dB);
    return true;
  }

  if (!this->write_channel_gain_(channel)) {
    ESP_LOGW(TAG, "%s writing %s Channel Volume: %ddb", ERROR, LR_CHANNEL_TEXT[channel], volume_dB);
    return false;
  }

  ESP_LOGD(TAG, "Set %s Channel Volume: %ddB", LR_CHANNEL_TEXT[channel], volume_dB);
#endif
  return true;
}

// used by select eq mode
void Tas58xxComponent::select_eq_mode(uint8_t select_index) {
  if ( select_index == static_cast<uint8_t>(EqMode::EQ_OFF) ) {
    this->set_eq_mode_(EqMode::EQ_OFF);
  } else {
    this->set_eq_mode_(this->configured_eq_mode_);
  }
}

// used by eq gain numbers
bool Tas58xxComponent::set_eq_gain(Channels channel, uint8_t band_index, int8_t gain) {
#ifdef USE_TAS58XX_EQ_GAINS

  if (band_index >= NUMBER_EQ_BANDS) {
    ESP_LOGE(TAG, "Invalid Band index: %d", band_index);
    return false;
  }

  const uint8_t band = band_index + 1;

  if (gain < TAS58XX_EQ_MIN_DB || gain > TAS58XX_EQ_MAX_DB) {
    ESP_LOGE(TAG, "Invalid %s Channel %s:%d Gain: %ddB", LR_CHANNEL_TEXT[channel], EQ_BAND, band, gain);
    return false;
  }

  this->tas58xx_eq_gain_[channel][band_index] = gain;

  // only save until ready to setup in 'loop'
  if (this->loop_setup_stage_ < EQ_BANDS_SETUP) {
    ESP_LOGV(TAG, "Save %s Channel %s:%d Gain: %ddB", LR_CHANNEL_TEXT[channel], EQ_BAND, band, gain);
    return true;
  }

  uint8_t x = (gain + TAS58XX_EQ_MAX_DB);

#ifdef USE_TAS5805M_DAC
  #ifdef USE_TAS58XX_EQ_BIAMP
  const AddressSequence* eq_address = (channel == LEFT_CHANNEL) ? &TAS5805M_LEFT_EQ_ADDRESS[band_index] : &TAS5805M_RIGHT_EQ_ADDRESS[band_index];
  #else
  const AddressSequence* eq_address = &TAS5805M_LEFT_EQ_ADDRESS[band_index];
  #endif
#else
  #ifdef USE_TAS58XX_EQ_BIAMP
  const AddressSequence* eq_address = (channel == LEFT_CHANNEL) ? &TAS5825M_LEFT_EQ_ADDRESS[band_index] : &TAS5825M_RIGHT_EQ_ADDRESS[band_index];
  #else
  const AddressSequence* eq_address = &TAS5825M_LEFT_EQ_ADDRESS[band_index];
  #endif
#endif

  const BiquadSequence* biquad = &EQ_BAND_COEFFICIENTS[x][band_index];

  if ((eq_address == NULL) || (biquad == NULL)) {
    ESP_LOGE(TAG, "%s NULL discovered %s Channel %s:%d Gain: %ddB", ERROR, LR_CHANNEL_TEXT[channel], EQ_BAND, band, gain);
    return false;
  }

  if (!this->book_and_page_write_(TAS58XX_EQ_CTRL_BOOK, eq_address->page, eq_address->sub_addr,
                                  reinterpret_cast<uint8_t*>(const_cast<uint8_t*>(biquad->coefficients)), BIQUAD_SIZE)) {
    ESP_LOGW(TAG, "%s writing Biquad %s Channel %s:%d Gain: %ddB", ERROR, LR_CHANNEL_TEXT[channel], EQ_BAND, band, gain);
    return false;
  }

  ESP_LOGD(TAG, "Set %s Channel %s:%d Gain: %ddB", LR_CHANNEL_TEXT[channel], EQ_BAND, band, gain);
#endif
  return true;
}

bool Tas58xxComponent::set_eq_preset(Channels channel, uint8_t select_preset) {
#ifdef USE_TAS58XX_EQ_PRESETS
  if (select_preset > EQ_PROFILE_MAXIMUM_INDEX) {
    ESP_LOGE(TAG, "Invalid %s Channel Preset index: %d", LR_CHANNEL_TEXT[channel], select_preset);
    return false;
  }

  this->tas58xx_channel_preset_[channel] = select_preset;

  // only save until ready to setup in 'loop'
  if (this->loop_setup_stage_ < EQ_PRESETS_SETUP) {
    ESP_LOGV(TAG, "Save %s Channel EQ Preset index: %d", LR_CHANNEL_TEXT[channel], select_preset);
    return true;
  }

#ifdef USE_TAS5805M_DAC
  const AddressSequence* biquad1_address = (channel == LEFT_CHANNEL) ? &TAS5805M_LEFT_EQ_ADDRESS[0] : &TAS5805M_RIGHT_EQ_ADDRESS[0];
  const AddressSequence* biquad2_address = (channel == LEFT_CHANNEL) ? &TAS5805M_LEFT_EQ_ADDRESS[1] : &TAS5805M_RIGHT_EQ_ADDRESS[1];
  const AddressSequence* biquad3_address = (channel == LEFT_CHANNEL) ? &TAS5805M_LEFT_EQ_ADDRESS[2] : &TAS5805M_RIGHT_EQ_ADDRESS[2];
#else
  const AddressSequence* biquad1_address = (channel == LEFT_CHANNEL) ? &TAS5825M_LEFT_EQ_ADDRESS[0] : &TAS5825M_RIGHT_EQ_ADDRESS[0];
  const AddressSequence* biquad2_address = (channel == LEFT_CHANNEL) ? &TAS5825M_LEFT_EQ_ADDRESS[1] : &TAS5825M_RIGHT_EQ_ADDRESS[1];
  const AddressSequence* biquad3_address = (channel == LEFT_CHANNEL) ? &TAS5825M_LEFT_EQ_ADDRESS[2] : &TAS5825M_RIGHT_EQ_ADDRESS[2];
#endif

  const BiquadSequence* biquad1 = (channel == LEFT_CHANNEL) ? &EQ_PROFILE_LEFT_COEFFICIENTS[select_preset][0] : &EQ_PROFILE_RIGHT_COEFFICIENTS[select_preset][0];
  const BiquadSequence* biquad2 = (channel == LEFT_CHANNEL) ? &EQ_PROFILE_LEFT_COEFFICIENTS[select_preset][1] : &EQ_PROFILE_RIGHT_COEFFICIENTS[select_preset][1];
  const BiquadSequence* biquad3 = (channel == LEFT_CHANNEL) ? &EQ_PROFILE_LEFT_COEFFICIENTS[select_preset][2] : &EQ_PROFILE_RIGHT_COEFFICIENTS[select_preset][2];

  if ((biquad1_address == NULL) || (biquad2_address == NULL) || (biquad3_address == NULL)) {
    ESP_LOGE(TAG, "NULL EQ Preset address pointer");
    return false;
  }

  if ((biquad1 == NULL) || (biquad2 == NULL) || (biquad3 == NULL)) {
    ESP_LOGE(TAG, "NULL EQ Preset coefficent pointer");
    return false;
  }

  if (!this->book_and_page_write_(TAS58XX_EQ_CTRL_BOOK, biquad1_address->page, biquad1_address->sub_addr,
                                  reinterpret_cast<uint8_t*>(const_cast<uint8_t*>(biquad1->coefficients)), BIQUAD_SIZE)) {
    ESP_LOGW(TAG, "%s writing Biquad 1 for %s Channel EQ Preset index: %d", ERROR, LR_CHANNEL_TEXT[channel], select_preset);
    return false;
  }
  if (!this->book_and_page_write_(TAS58XX_EQ_CTRL_BOOK, biquad2_address->page, biquad2_address->sub_addr,
                                  reinterpret_cast<uint8_t*>(const_cast<uint8_t*>(biquad2->coefficients)), BIQUAD_SIZE)) {
    ESP_LOGW(TAG, "%s writing Biquad 2 for %s Channel EQ Preset index: %d", ERROR, LR_CHANNEL_TEXT[channel], select_preset);
    return false;
  }
  if (!this->book_and_page_write_(TAS58XX_EQ_CTRL_BOOK, biquad3_address->page, biquad3_address->sub_addr,
                                  reinterpret_cast<uint8_t*>(const_cast<uint8_t*>(biquad3->coefficients)), BIQUAD_SIZE)) {
    ESP_LOGW(TAG, "%s writing Biquad 3 for %s Channel EQ Preset index: %d", ERROR, LR_CHANNEL_TEXT[channel], select_preset);
    return false;
  }

  ESP_LOGD(TAG, "Set %s Channel EQ Preset index >> %d", LR_CHANNEL_TEXT[channel], select_preset);
#endif
  return true;
}

bool Tas58xxComponent::set_mute_off() {
  if (!this->is_muted_) return true;
  this->is_muted_ = false;
  // safety mute keeps the DAC muted until the crossover is in place
  if (!this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_2, this->tas58xx_control_state_ + this->ctrl2_mute_bits_())) {
    this->is_muted_ = true;
    return false;
  }
  ESP_LOGV(TAG, "Mute >> Off");
  return true;
}

// set bit 3 MUTE in TAS58XX_DEVICE_CTRL_2 and retain current Control State
// ensures get_state = get_power_state
bool Tas58xxComponent::set_mute_on() {
  if (this->is_muted_) return true;
  if (!this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_2, this->tas58xx_control_state_ + TAS58XX_MUTE_CONTROL)) return false;
  this->is_muted_ = true;
  ESP_LOGV(TAG, "Mute >> On");
  return true;
}

// used by fault sensor
uint32_t Tas58xxComponent::times_faults_cleared() {
  return this->times_faults_cleared_;
}

// used by 'left_gain_band16000hz' or 'right_gain_band16000hz' or 'select eq_mode'
bool Tas58xxComponent::using_auto_eq_refresh() {
  return (this->eq_refresh_ == EqRefreshMode::AUTO);
}

// used by 'select eq_mode'
bool Tas58xxComponent::using_manual_eq_refresh() {
  return (this->eq_refresh_ == EqRefreshMode::MANUAL);
}

float Tas58xxComponent::volume() {
  uint8_t raw_volume;
  this->get_digital_volume_(&raw_volume);
  return remap<float, uint8_t>(raw_volume, this->tas58xx_raw_volume_min_, this->tas58xx_raw_volume_max_, 0.0f, 1.0f);
}

bool Tas58xxComponent::set_volume(float volume) {
  float new_volume = clamp(volume, 0.0f, 1.0f);
  uint8_t raw_volume = remap<uint8_t, float>(new_volume, 0.0f, 1.0f, this->tas58xx_raw_volume_min_, this->tas58xx_raw_volume_max_);
  if (!this->set_digital_volume_(raw_volume)) return false;
#ifdef USE_TAS58XX_EQ_CROSSOVER
  // digital volume register is 24dB to -103dB in 0.5dB steps
  this->xover_volume_db_ = 24.0f - (raw_volume / 2.0f);
  this->update_loudness_();
#endif
  #if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
    int8_t dB = -(raw_volume / 2) + 24;
    ESP_LOGV(TAG, "Volume >> %ddB", dB);
  #endif
  return true;
}

// protected //

bool Tas58xxComponent::get_analog_gain_(uint8_t* raw_gain) {
  uint8_t current;
  if (!this->tas58xx_read_bytes_(TAS58XX_AGAIN, &current, 1)) return false;
  // remove top 3 reserved bits
  *raw_gain = current & 0x1F;
  return true;
}

// Analog Gain Control , with 0.5dB one step
// lower 5 bits controls the analog gain.
// 00000: 0 dB (29.5V peak voltage)
// 00001: -0.5db
// 11111: -15.5 dB
// set analog gain in dB
bool Tas58xxComponent::set_analog_gain_(float gain_db) {
  static constexpr uint8_t TOP_3BITS_MASK = 0xE0;

  if ((gain_db < TAS58XX_MIN_ANALOG_GAIN) || (gain_db > TAS58XX_MAX_ANALOG_GAIN)) return false;

  uint8_t new_again = static_cast<uint8_t>(-gain_db * 2.0);

  uint8_t current_again;
  if (!this->tas58xx_read_bytes_(TAS58XX_AGAIN, &current_again, 1)) return false;

  // keep top 3 reserved bits combine with bottom 5 analog gain bits
  new_again = (current_again & TOP_3BITS_MASK) | new_again;
  if (!this->tas58xx_write_byte_(TAS58XX_AGAIN, new_again)) return false;

  ESP_LOGV(TAG, "Analog Gain >> %fdB", gain_db);
  return true;
}

bool Tas58xxComponent::get_dac_mode_(DacMode* mode) {
    uint8_t current_value;
    if (!this->tas58xx_read_bytes_(TAS58XX_DEVICE_CTRL_1, &current_value, 1)) return false;
    if (current_value & (1 << 2)) {
        *mode = PBTL;
    } else {
        *mode = BTL;
    }
    this->tas58xx_dac_mode_ = *mode;
    return true;
}

// only runs once from 'setup'
bool Tas58xxComponent::set_dac_mode_(DacMode mode) {
  uint8_t current_value;
  if (!this->tas58xx_read_bytes_(TAS58XX_DEVICE_CTRL_1, &current_value, 1)) return false;

  // Update bit 2 based on the mode
  if (mode == PBTL) {
      current_value |= (1 << 2);  // Set bit 2 to 1 (PBTL mode)
  } else {
      current_value &= ~(1 << 2); // Clear bit 2 to 0 (BTL mode)
  }
  if (!this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_1, current_value)) return false;

  // save so 'set_dac_mode_' could be used more generally
  this->tas58xx_dac_mode_ = mode;
  ESP_LOGV(TAG, "DAC mode >> %s", this->tas58xx_dac_mode_ ? "PBTL" : "BTL");
  return true;
}

bool Tas58xxComponent::set_deep_sleep_off_() {
  if (this->tas58xx_control_state_ != CTRL_DEEP_SLEEP) return true; // already not in deep sleep
  // preserve mute state
  uint8_t new_value = CTRL_PLAY + this->ctrl2_mute_bits_();
  if (!this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_2, new_value)) return false;

  this->tas58xx_control_state_ = CTRL_PLAY;                        // set Control State to play
  ESP_LOGV(TAG, "Deep Sleep >> Off");
  #if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  if (this->is_muted_) ESP_LOGV(TAG, "Mute On preserved");
  #endif
  return true;
}

bool Tas58xxComponent::set_deep_sleep_on_() {
  if (this->tas58xx_control_state_ == CTRL_DEEP_SLEEP) return true; // already in deep sleep

  // preserve mute state
  uint8_t new_value = CTRL_DEEP_SLEEP + this->ctrl2_mute_bits_();
  if (!this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_2, new_value)) return false;

  this->tas58xx_control_state_ = CTRL_DEEP_SLEEP;                   // set Control State to deep sleep
  ESP_LOGV(TAG, "Deep Sleep >> On");
  #if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  if (this->is_muted_) ESP_LOGV(TAG, "Mute On preserved");
  #endif
  return true;
}

bool Tas58xxComponent::get_digital_volume_(uint8_t* raw_volume) {
  uint8_t current = 254; // lowest raw volume
  if (!this->tas58xx_read_bytes_(TAS58XX_DIG_VOL_CTRL, &current, 1)) return false;
  *raw_volume = current;
  return true;
}

// controls both left and right channel digital volume
// digital volume is 24 dB to -103 dB in -0.5 dB step
// 00000000: +24.0 dB
// 00000001: +23.5 dB
// 00101111: +0.5 dB
// 00110000: 0.0 dB
// 00110001: -0.5 dB
// 11111110: -103 dB
// 11111111: Mute
bool Tas58xxComponent::set_digital_volume_(uint8_t raw_volume) {
  if (!this->tas58xx_write_byte_(TAS58XX_DIG_VOL_CTRL, raw_volume)) return false;
  return true;
}

bool Tas58xxComponent::get_eq_mode_(EqMode* current_mode) {
  *current_mode = this->tas58xx_eq_mode_;
  return true;
}

bool Tas58xxComponent::set_eq_mode_(EqMode new_mode) {
#if defined(USE_TAS58XX_EQ_GAINS) || defined(USE_TAS58XX_EQ_PRESETS) || defined(USE_TAS58XX_EQ_CROSSOVER)
  [[maybe_unused]] const bool mode_changed = (this->tas58xx_eq_mode_ != new_mode);
  this->tas58xx_eq_mode_ = new_mode;

  // only save until ready to setup in 'loop'
  if (this->loop_setup_stage_ < INPUT_MIXER_SETUP) {
    ESP_LOGV(TAG, "Save EQ Mode: %s", EQ_MODE_TEXT[new_mode]);
    return true;
  }

#ifdef USE_TAS58XX_EQ_CROSSOVER
  // switching EQ mode changes what the tweeter receives, so reapply under the safety mute
  // EQ Off bypasses the crossover, so the tweeter channel gain is then held at mute
  if (this->loop_setup_stage_ >= CROSSOVER_SETUP) {
    if (!mode_changed) return true;
    ESP_LOGD(TAG, "Set EQ Mode: %s", EQ_MODE_TEXT[new_mode]);
    this->request_crossover_apply_(false);
    return true;
  }
#endif

  if (!this->write_eq_mode_register_(new_mode)) return false;

  ESP_LOGD(TAG, "Set EQ Mode: %s", EQ_MODE_TEXT[new_mode]);
#endif
  return true;
}

bool Tas58xxComponent::write_eq_mode_register_(EqMode new_mode) {
#ifdef USE_TAS5805M_DAC
  if (!this->tas58xx_write_byte_(TAS5805M_DSP_MISC, TAS5805M_CTRL_EQ[new_mode])) {
    ESP_LOGW(TAG, "%s writing Eq Mode: %s", ERROR, EQ_MODE_TEXT[new_mode]);
    return false;
  }
#else
  const EqModeCoefficients* eq_mode_coefficients = &TAS5825M_CTRL_EQ[new_mode];
  if (!this->book_and_page_write_(TAS58XX_AUDIO_CTRL_BOOK, TAS5825M_EQ_MODE_CTRL_PAGE, TAS5825M_GANG_EQ,
                                  reinterpret_cast<uint8_t*>(const_cast<EqModeCoefficients*>(eq_mode_coefficients)), sizeof(EqModeCoefficients))) {
    ESP_LOGW(TAG, "%s writing Eq Mode: %s", ERROR, EQ_MODE_TEXT[new_mode]);
    return false;
  }
#endif
  return true;
}

// only runs once from 'setup'
bool Tas58xxComponent::set_modulation_scheme_(ModulationScheme modulation) {
  static constexpr uint8_t MODULATION_MASK = 0b11111100; // bits 0 and 1 are modulation

  uint8_t value;
  if (!this->tas58xx_read_bytes_(TAS58XX_DEVICE_CTRL_1, &value, 1)) return false;

  value = value & (MODULATION_MASK + static_cast<uint8_t>(modulation));

  if (!this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_1, value)) return false;

  // save so 'set_modulation_scheme_' could be used more generally
  this->tas58xx_modulation_scheme_ = modulation;
  ESP_LOGV(TAG, "Modulation >> %s", this->tas58xx_modulation_scheme_ ? "1SPW Mode" : "BD Mode");
  return true;
}

bool Tas58xxComponent::get_state_(ControlState* state) {
  *state = this->tas58xx_control_state_;
  return true;
}

bool Tas58xxComponent::set_state_(ControlState state) {
  if (this->tas58xx_control_state_ == state) return true;
  if (!this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_2, state + this->ctrl2_mute_bits_())) return false;
  this->tas58xx_control_state_ = state;
  return true;
}

uint8_t Tas58xxComponent::ctrl2_mute_bits_() {
  return (this->is_muted_ || this->safety_mute_) ? TAS58XX_MUTE_CONTROL : 0;
}

//// fault processing functions

bool Tas58xxComponent::clear_fault_registers_() {
  if (!this->tas58xx_write_byte_(TAS58XX_FAULT_CLEAR, TAS58XX_ANALOG_FAULT_CLEAR)) return false;
  this->times_faults_cleared_++;
  ESP_LOGD(TAG, "Faults cleared");
  return true;
}

#ifdef USE_TAS58XX_BINARY_SENSOR
void Tas58xxComponent::publish_faults_() {
  if (this->is_new_common_fault_) {
    if (this->have_fault_binary_sensor_ != nullptr) {
      this->have_fault_binary_sensor_->publish_state(this->tas58xx_faults_.have_fault);
    }

    if (this->clock_fault_binary_sensor_ != nullptr) {
      this->clock_fault_binary_sensor_->publish_state(this->tas58xx_faults_.clock_fault);
    }
  }

  if (this->is_new_over_temperature_issue_) {
    if (this->over_temperature_shutdown_fault_binary_sensor_ != nullptr) {
      this->over_temperature_shutdown_fault_binary_sensor_->publish_state(this->tas58xx_faults_.temperature_fault);
    }

    if (this->over_temperature_warning_binary_sensor_ != nullptr) {
      this->over_temperature_warning_binary_sensor_->publish_state(this->tas58xx_faults_.temperature_warning);
    }
  }

  // publish channel and global faults in separate loop iterations to spread component time when publishing binary sensors
  if (this->is_new_channel_fault_) {
    this->set_timeout("", 15, [this]() { this->publish_channel_faults_(); });
  }
  else {
    if (this->is_new_global_fault_) {
      this->set_timeout("", 15, [this]() { this->publish_global_faults_(); });
    }
  }
}

void Tas58xxComponent::publish_channel_faults_() {
  if (this->right_channel_over_current_fault_binary_sensor_ != nullptr) {
    this->right_channel_over_current_fault_binary_sensor_->publish_state(this->tas58xx_faults_.channel_fault & (1 << 0));
  }

  if (this->left_channel_over_current_fault_binary_sensor_ != nullptr) {
    this->left_channel_over_current_fault_binary_sensor_->publish_state(this->tas58xx_faults_.channel_fault & (1 << 1));
  }

  if (this->right_channel_dc_fault_binary_sensor_ != nullptr) {
    this->right_channel_dc_fault_binary_sensor_->publish_state(this->tas58xx_faults_.channel_fault & (1 << 2));
  }

  if (this->left_channel_dc_fault_binary_sensor_ != nullptr) {
    this->left_channel_dc_fault_binary_sensor_->publish_state(this->tas58xx_faults_.channel_fault & (1 << 3));
  }

  if (this->is_new_global_fault_) {
      this->set_timeout("", 15, [this]() { this->publish_global_faults_(); });
  }
}


void Tas58xxComponent::publish_global_faults_() {
  if (this->pvdd_under_voltage_fault_binary_sensor_ != nullptr) {
    this->pvdd_under_voltage_fault_binary_sensor_->publish_state(this->tas58xx_faults_.global_fault & (1 << 0));
  }

  if (this->pvdd_over_voltage_fault_binary_sensor_ != nullptr) {
    this->pvdd_over_voltage_fault_binary_sensor_->publish_state(this->tas58xx_faults_.global_fault & (1 << 1));
  }

  if (this->bq_write_failed_fault_binary_sensor_ != nullptr) {
    this->bq_write_failed_fault_binary_sensor_->publish_state(this->tas58xx_faults_.global_fault & (1 << 6));
  }

  if (this->otp_crc_check_error_binary_sensor_ != nullptr) {
    this->otp_crc_check_error_binary_sensor_->publish_state(this->tas58xx_faults_.global_fault & (1 << 7));
  }
}
#endif

bool Tas58xxComponent::read_fault_registers_() {
  static constexpr uint8_t REMOVE_CLOCK_FAULT = 0xFB;  // clock fault bit of global_fault1 register

  uint8_t current_faults[4];

  // read all faults registers
  if (!this->tas58xx_read_bytes_(TAS58XX_CHAN_FAULT, current_faults, sizeof(current_faults))) return false;

  // note: new state is saved regardless as it is not worth conditionally saving state based on whether state has changed

  // check if any change CHAN_FAULT register as it contains 4 fault conditions(binary sensors)
  this->is_new_channel_fault_ = (current_faults[0] != this->tas58xx_faults_.channel_fault);
  this->tas58xx_faults_.channel_fault = current_faults[0];

  // separate clock fault from GLOBAL_FAULT1 register since clock faults can occur often
  // check if any change in GLOBAL_FAULT1 register as it contains 4 fault conditions(binary sensors) excluding clock fault
  uint8_t current_global_fault = current_faults[1] & REMOVE_CLOCK_FAULT;
  this->is_new_global_fault_ = (current_global_fault != this->tas58xx_faults_.global_fault);
  this->tas58xx_faults_.global_fault = current_global_fault;

  // over temperature fault is only fault condition in global_fault2 register
  this->is_new_over_temperature_issue_ = (current_faults[2] != this->tas58xx_faults_.temperature_fault);
  this->tas58xx_faults_.temperature_fault = current_faults[2];

  // over temperature warning is only fault condition in ot_warning register
  this->is_new_over_temperature_issue_ = (this->is_new_over_temperature_issue_ || (current_faults[3] != this->tas58xx_faults_.temperature_warning));
  this->tas58xx_faults_.temperature_warning = current_faults[3];

  bool new_fault_state; // reuse for temporary storage of new fault state

  // process clock_fault binary sensor
  new_fault_state = (current_faults[1] & (1 << 2));
  this->is_new_common_fault_ = (new_fault_state != this->tas58xx_faults_.clock_fault);
  this->tas58xx_faults_.clock_fault = new_fault_state;

  this->tas58xx_faults_.is_fault_except_clock_fault =
    ( this->tas58xx_faults_.channel_fault || this->tas58xx_faults_.global_fault ||
      this->tas58xx_faults_.temperature_fault || this->tas58xx_faults_.temperature_warning );

#ifdef USE_TAS58XX_BINARY_SENSOR
  // process have_fault binary sensor
  new_fault_state = (this->tas58xx_faults_.is_fault_except_clock_fault || (this->tas58xx_faults_.clock_fault && (!this->exclude_clock_fault_from_have_faults_)));
  this->is_new_common_fault_ = this->is_new_common_fault_ || (new_fault_state != this->tas58xx_faults_.have_fault);
  this->tas58xx_faults_.have_fault = new_fault_state;
#endif

  return true;
}

//// channel gain

#if defined(USE_TAS58XX_CHANNEL_VOLUMES) || defined(USE_TAS58XX_EQ_CROSSOVER)
bool Tas58xxComponent::write_channel_gain_(Channels channel) {
  int32_t little_endian_9_23 = tas58xx_helpers::gain_to_f9_23_(this->tas58xx_channel_volume_[channel]);

#ifdef USE_TAS58XX_EQ_CROSSOVER
  // tweeter channel gain is held at mute whenever the crossover is not protecting it
  if ((channel == this->crossover_channel_(crossover::ROLE_TWEETER)) && this->tweeter_must_be_muted_()) {
    little_endian_9_23 = 0;
  }
  // driver muted from Home Assistant, e.g. to measure the other driver on its own
  const uint8_t role = (channel == this->crossover_channel_(crossover::ROLE_TWEETER)) ? crossover::ROLE_TWEETER : crossover::ROLE_MID;
  if (this->xover_mute_[role]) little_endian_9_23 = 0;
#endif

  return this->book_and_page_write_(TAS58XX_AUDIO_CTRL_BOOK, TAS58XX_CHANNEL_VOLUME_PAGE, TAS58XX_CHANNEL_VOLUME_SUBADDR[channel],
                                    reinterpret_cast<uint8_t*>(&little_endian_9_23), sizeof(little_endian_9_23));
}
#endif

#ifdef USE_TAS58XX_EQ_CROSSOVER
//// crossover

float Tas58xxComponent::get_crossover_value(CrossoverParam param, uint8_t role, uint8_t band) {
  if (role >= crossover::NUMBER_ROLES || band >= crossover::NUMBER_PEQ_BANDS) return 0.0f;
  const crossover::DriverSettings &driver = this->xover_.driver[role];
  const crossover::PeqBand &peq = driver.peq[band];

  switch (param) {
    case XO_PARAM_FILTER:             return static_cast<float>(this->xover_.filter);
    case XO_PARAM_TWEETER_CHANNEL:    return static_cast<float>(this->xover_tweeter_channel_);
    case XO_PARAM_FREQUENCY:          return this->xover_.frequency;
    case XO_PARAM_GAIN:               return driver.gain_db;
    case XO_PARAM_INVERT:             return driver.invert ? 1.0f : 0.0f;
    case XO_PARAM_DELAY:              return driver.delay_mm;
    case XO_PARAM_LOUDNESS:           return this->xover_.loudness ? 1.0f : 0.0f;
    case XO_PARAM_LOUDNESS_REFERENCE: return this->xover_.loudness_reference_db;
    case XO_PARAM_PEQ_FREQUENCY:      return peq.frequency;
    case XO_PARAM_PEQ_GAIN:           return peq.gain_db;
    case XO_PARAM_PEQ_Q:              return peq.q;
    case XO_PARAM_PEQ_TYPE:           return static_cast<float>(peq.type);
    case XO_PARAM_SUBSONIC_FREQUENCY: return this->xover_.subsonic_frequency;
    case XO_PARAM_BAFFLE_WIDTH:       return this->xover_.baffle_width_cm;
    case XO_PARAM_BAFFLE_PLACEMENT:   return static_cast<float>(this->xover_.baffle_placement);
    case XO_PARAM_NOTCH_FREQUENCY:    return this->xover_.notch_frequency;
    case XO_PARAM_NOTCH_GAIN:         return this->xover_.notch_gain_db;
    case XO_PARAM_NOTCH_Q:            return this->xover_.notch_q;
    case XO_PARAM_AIR_GAIN:           return this->xover_.air_gain_db;
    case XO_PARAM_LINKWITZ_F0:        return this->xover_.linkwitz_f0;
    case XO_PARAM_LINKWITZ_Q0:        return this->xover_.linkwitz_q0;
    case XO_PARAM_LINKWITZ_FP:        return this->xover_.linkwitz_fp;
    case XO_PARAM_LINKWITZ_QP:        return this->xover_.linkwitz_qp;
    case XO_PARAM_MUTE:               return this->xover_mute_[role] ? 1.0f : 0.0f;
    case XO_PARAM_HEADROOM_COMPENSATION: return this->xover_.headroom_compensation ? 1.0f : 0.0f;
  }
  return 0.0f;
}

void Tas58xxComponent::set_crossover_value(CrossoverParam param, uint8_t role, uint8_t band, float value) {
  if (role >= crossover::NUMBER_ROLES || band >= crossover::NUMBER_PEQ_BANDS) {
    ESP_LOGE(TAG, "Invalid %s role:%d band:%d", CROSSOVER, role, band);
    return;
  }
  this->xover_peak_valid_ = false;
  const crossover::DriverRole driver_role = static_cast<crossover::DriverRole>(role);
  crossover::DriverSettings &driver = this->xover_.driver[role];
  crossover::PeqBand &peq = driver.peq[band];

  switch (param) {
    case XO_PARAM_FILTER: {
      const uint8_t filter = static_cast<uint8_t>(value);
      if (filter >= crossover::NUMBER_FILTERS) {
        ESP_LOGE(TAG, "Invalid %s Filter index: %d", CROSSOVER, filter);
        return;
      }
      this->xover_.filter = static_cast<crossover::CrossoverFilter>(filter);
      ESP_LOGD(TAG, "Set %s Filter: %s", CROSSOVER, FILTER_TEXT[filter]);
      this->crossover_sections_changed_();
      return;
    }

    case XO_PARAM_FREQUENCY:
      this->xover_.frequency = clamp(value, crossover::FREQUENCY_MIN, crossover::FREQUENCY_MAX);
      if (this->xover_.frequency < this->xover_.frequency_min) {
        ESP_LOGW(TAG, "%s Frequency %.0fHz is below the tweeter minimum, using %.0fHz", CROSSOVER,
                 this->xover_.frequency, crossover::effective_frequency(this->xover_));
      }
      ESP_LOGD(TAG, "Set %s Frequency: %.0fHz", CROSSOVER, crossover::effective_frequency(this->xover_));
      this->crossover_sections_changed_();
      return;

    case XO_PARAM_TWEETER_CHANNEL: {
      const Channels channel = (value >= 0.5f) ? RIGHT_CHANNEL : LEFT_CHANNEL;
      if (channel == this->xover_tweeter_channel_) return;
      this->xover_tweeter_channel_ = channel;
      ESP_LOGD(TAG, "Set %s Tweeter Channel: %s", CROSSOVER, LR_CHANNEL_TEXT[channel]);
      // both channels change filter type, so rewrite everything under the safety mute
      this->request_crossover_apply_(false);
      return;
    }

    case XO_PARAM_GAIN:
      driver.gain_db = clamp(value, crossover::DRIVER_GAIN_MIN_DB, crossover::DRIVER_GAIN_MAX_DB);
      ESP_LOGD(TAG, "Set %s %s Gain: %.1fdB", CROSSOVER, ROLE_TEXT[role], driver.gain_db);
      this->crossover_band_changed_(driver_role, crossover::BAND_GAIN);
      return;

    case XO_PARAM_INVERT:
      driver.invert = (value >= 0.5f);
      ESP_LOGD(TAG, "Set %s %s Polarity: %s", CROSSOVER, ROLE_TEXT[role], driver.invert ? "Inverted" : "Normal");
      this->crossover_band_changed_(driver_role, crossover::BAND_GAIN);
      return;

    case XO_PARAM_DELAY:
      driver.delay_mm = clamp(value, 0.0f, crossover::DELAY_MAX_MM);
      ESP_LOGD(TAG, "Set %s %s Delay: %.0fmm", CROSSOVER, ROLE_TEXT[role], driver.delay_mm);
      this->crossover_band_changed_(driver_role, crossover::BAND_DELAY);
      return;

    case XO_PARAM_LOUDNESS:
      this->xover_.loudness = (value >= 0.5f);
      ESP_LOGD(TAG, "Set Loudness: %s", ONOFF(this->xover_.loudness));
      this->update_loudness_();
      return;

    case XO_PARAM_LOUDNESS_REFERENCE:
      this->xover_.loudness_reference_db = clamp(value, crossover::LOUDNESS_REFERENCE_MIN_DB, crossover::LOUDNESS_REFERENCE_MAX_DB);
      ESP_LOGD(TAG, "Set Loudness Reference: %.1fdB", this->xover_.loudness_reference_db);
      this->update_loudness_();
      return;

    case XO_PARAM_PEQ_FREQUENCY:
      peq.frequency = clamp(value, crossover::PEQ_FREQUENCY_MIN, crossover::PEQ_FREQUENCY_MAX);
      ESP_LOGD(TAG, "Set %s PEQ %d Frequency: %.0fHz", ROLE_TEXT[role], band + 1, peq.frequency);
      break;

    case XO_PARAM_PEQ_GAIN:
      peq.gain_db = clamp(value, -crossover::PEQ_GAIN_MAX_DB, crossover::PEQ_GAIN_MAX_DB);
      ESP_LOGD(TAG, "Set %s PEQ %d Gain: %.1fdB", ROLE_TEXT[role], band + 1, peq.gain_db);
      break;

    case XO_PARAM_PEQ_Q:
      peq.q = clamp(value, crossover::PEQ_Q_MIN, crossover::PEQ_Q_MAX);
      ESP_LOGD(TAG, "Set %s PEQ %d Q: %.2f", ROLE_TEXT[role], band + 1, peq.q);
      break;

    case XO_PARAM_PEQ_TYPE: {
      const uint8_t type = static_cast<uint8_t>(value);
      if (type >= crossover::NUMBER_PEQ_TYPES) {
        ESP_LOGE(TAG, "Invalid %s PEQ %d Type index: %d", ROLE_TEXT[role], band + 1, type);
        return;
      }
      peq.type = static_cast<crossover::PeqType>(type);
      ESP_LOGD(TAG, "Set %s PEQ %d Type index: %d", ROLE_TEXT[role], band + 1, type);
      break;
    }

    // speaker compensation bands, none of them can remove the tweeter high-pass
    case XO_PARAM_SUBSONIC_FREQUENCY:
      this->xover_.subsonic_frequency = clamp(value, 0.0f, crossover::SUBSONIC_FREQUENCY_MAX);
      ESP_LOGD(TAG, "Set Subsonic Filter: %.0fHz", this->xover_.subsonic_frequency);
      // poles close to the unit circle, a half written biquad can go unstable, so write under the safety mute
      this->request_crossover_apply_(false);
      return;

    case XO_PARAM_BAFFLE_WIDTH:
      this->xover_.baffle_width_cm = clamp(value, 0.0f, crossover::BAFFLE_WIDTH_MAX_CM);
      ESP_LOGD(TAG, "Set Baffle Width: %.0fcm", this->xover_.baffle_width_cm);
      this->crossover_band_changed_(crossover::ROLE_MID, crossover::BAND_BAFFLE_STEP);
      return;

    case XO_PARAM_BAFFLE_PLACEMENT: {
      const uint8_t placement = static_cast<uint8_t>(value);
      if (placement >= crossover::NUMBER_PLACEMENTS) {
        ESP_LOGE(TAG, "Invalid Baffle Placement index: %d", placement);
        return;
      }
      this->xover_.baffle_placement = static_cast<crossover::BafflePlacement>(placement);
      ESP_LOGD(TAG, "Set Baffle Placement index: %d", placement);
      this->crossover_band_changed_(crossover::ROLE_MID, crossover::BAND_BAFFLE_STEP);
      return;
    }

    case XO_PARAM_NOTCH_FREQUENCY:
      this->xover_.notch_frequency = clamp(value, 0.0f, crossover::NOTCH_FREQUENCY_MAX);
      ESP_LOGD(TAG, "Set Tweeter Notch Frequency: %.0fHz", this->xover_.notch_frequency);
      this->crossover_band_changed_(crossover::ROLE_TWEETER, crossover::BAND_NOTCH);
      return;

    case XO_PARAM_NOTCH_GAIN:
      this->xover_.notch_gain_db = clamp(value, crossover::NOTCH_GAIN_MIN_DB, 0.0f);
      ESP_LOGD(TAG, "Set Tweeter Notch Gain: %.1fdB", this->xover_.notch_gain_db);
      this->crossover_band_changed_(crossover::ROLE_TWEETER, crossover::BAND_NOTCH);
      return;

    case XO_PARAM_NOTCH_Q:
      this->xover_.notch_q = clamp(value, crossover::NOTCH_Q_MIN, crossover::NOTCH_Q_MAX);
      ESP_LOGD(TAG, "Set Tweeter Notch Q: %.1f", this->xover_.notch_q);
      this->crossover_band_changed_(crossover::ROLE_TWEETER, crossover::BAND_NOTCH);
      return;

    case XO_PARAM_AIR_GAIN:
      this->xover_.air_gain_db = clamp(value, -crossover::AIR_GAIN_MAX_DB, crossover::AIR_GAIN_MAX_DB);
      ESP_LOGD(TAG, "Set Air Shelf Gain: %.1fdB", this->xover_.air_gain_db);
      this->crossover_band_changed_(crossover::ROLE_TWEETER, crossover::BAND_AIR);
      return;

    case XO_PARAM_LINKWITZ_F0:
    case XO_PARAM_LINKWITZ_FP:
      // 0 turns the Linkwitz transform off
      (param == XO_PARAM_LINKWITZ_F0 ? this->xover_.linkwitz_f0 : this->xover_.linkwitz_fp) =
          clamp(value, 0.0f, crossover::LINKWITZ_FREQUENCY_MAX);
      ESP_LOGD(TAG, "Set Linkwitz Transform: f0 %.0fHz -> fp %.0fHz", this->xover_.linkwitz_f0, this->xover_.linkwitz_fp);
      // low frequency poles like the subsonic filter, write under the safety mute
      this->request_crossover_apply_(false);
      return;

    case XO_PARAM_LINKWITZ_Q0:
    case XO_PARAM_LINKWITZ_QP:
      (param == XO_PARAM_LINKWITZ_Q0 ? this->xover_.linkwitz_q0 : this->xover_.linkwitz_qp) =
          clamp(value, crossover::LINKWITZ_Q_MIN, crossover::LINKWITZ_Q_MAX);
      ESP_LOGD(TAG, "Set Linkwitz Transform: Q0 %.2f -> Qp %.2f", this->xover_.linkwitz_q0, this->xover_.linkwitz_qp);
      this->request_crossover_apply_(false);
      return;

    case XO_PARAM_MUTE: {
      this->xover_mute_[role] = (value >= 0.5f);
      ESP_LOGD(TAG, "Set %s Mute: %s", ROLE_TEXT[role], ONOFF(this->xover_mute_[role]));
      // channel gain registers need the I2S clock, before that the setup sequence writes them
      if (this->loop_setup_stage_ > LR_VOLUME_SETUP && !this->write_channel_gain_(this->crossover_channel_(driver_role))) {
        ESP_LOGW(TAG, "%s writing %s Mute", ERROR, ROLE_TEXT[role]);
      }
      return;
    }

    case XO_PARAM_HEADROOM_COMPENSATION:
      this->xover_.headroom_compensation = (value >= 0.5f);
      ESP_LOGD(TAG, "Set Headroom Compensation: %s", ONOFF(this->xover_.headroom_compensation));
      this->crossover_band_changed_(crossover::ROLE_TWEETER, crossover::BAND_GAIN);
      this->crossover_band_changed_(crossover::ROLE_MID, crossover::BAND_GAIN);
      return;

    default:
      return;
  }

  // PEQ parameters
  this->crossover_band_changed_(driver_role, crossover::BAND_PEQ_START + band);
}

bool Tas58xxComponent::tweeter_must_be_muted_() {
  // EQ Off bypasses the DSP biquads, which would send full range audio to the tweeter
  return !this->xover_applied_ || (this->tas58xx_eq_mode_ == EQ_OFF);
}

Channels Tas58xxComponent::crossover_channel_(crossover::DriverRole role) {
  if (role == crossover::ROLE_TWEETER) return this->xover_tweeter_channel_;
  return (this->xover_tweeter_channel_ == LEFT_CHANNEL) ? RIGHT_CHANNEL : LEFT_CHANNEL;
}

void Tas58xxComponent::request_crossover_apply_(bool verify_only) {
  // a full apply always wins over a verify only request
  if (!this->xover_pending_) {
    this->xover_verify_only_ = verify_only;
  } else if (!verify_only) {
    this->xover_verify_only_ = false;
  }
  this->xover_pending_ = true;
  if (this->loop_setup_stage_ == SETUP_COMPLETE) this->enable_loop();
}

// Applies the whole crossover while the DAC is muted. Each step runs in its own 'loop' iteration.
// The safety mute is only released once the tweeter biquads have been read back and match.
bool Tas58xxComponent::crossover_loop_() {
  switch (this->xover_step_) {
    case XO_IDLE:
      if (!this->xover_pending_) return false;
      if (this->xover_wait_loops_ > 0) {
        this->xover_wait_loops_--;
        return true;
      }
      if (!this->set_safety_mute_(true)) {
        this->xover_failures_++;
        ESP_LOGW(TAG, "%s %s setting safety mute, retry %d", ERROR, CROSSOVER, this->xover_failures_);
        this->xover_wait_loops_ = RETRY_INTERVAL;
        return true;
      }
      this->xover_pending_ = false;
      this->xover_applied_ = false;
      this->xover_clock_good_reads_ = 0;
      this->xover_wait_loops_ = 0;
      this->xover_step_ = XO_WAIT_CLOCK;
      return true;

    case XO_WAIT_CLOCK:
      if (!this->wait_for_stable_clock_()) return true;
      this->xover_step_ = this->xover_verify_only_ ? XO_VERIFY : XO_WRITE_MODE;
      return true;

    case XO_WRITE_MODE:
      this->xover_verify_only_ = false;
      this->xover_loudness_applied_ = crossover::loudness_boost(this->xover_, this->xover_volume_db_);
      this->xover_peak_valid_ = false;
      if (!this->write_eq_mode_register_(this->tas58xx_eq_mode_)) {
        this->crossover_step_failed_("EQ mode write");
        return true;
      }
      if (!this->set_mixer_mode(this->tas58xx_mixer_mode_)) {
        this->crossover_step_failed_("mixer write");
        return true;
      }
      this->xover_step_ = XO_WRITE_TWEETER;
      return true;

    case XO_WRITE_TWEETER:
      if (!this->write_crossover_chain_(crossover::ROLE_TWEETER)) {
        this->crossover_step_failed_("tweeter biquad write");
        return true;
      }
      this->xover_step_ = XO_WRITE_MID;
      return true;

    case XO_WRITE_MID:
      if (!this->write_crossover_chain_(crossover::ROLE_MID)) {
        this->crossover_step_failed_("mid biquad write");
        return true;
      }
      this->xover_step_ = XO_VERIFY;
      return true;

    case XO_VERIFY:
      if (!this->verify_crossover_chain_(crossover::ROLE_TWEETER) || !this->verify_crossover_chain_(crossover::ROLE_MID)) {
        if (this->xover_verify_only_) {
          // crossover was lost or changed while the DAC was asleep, write it again
          ESP_LOGI(TAG, "%s readback differs, rewriting", CROSSOVER);
          this->xover_verify_only_ = false;
          this->xover_step_ = XO_WRITE_MODE;
          return true;
        }
        this->crossover_step_failed_("readback");
        return true;
      }
      this->xover_verify_only_ = false;
      this->xover_step_ = XO_WRITE_GAINS;
      return true;

    case XO_WRITE_GAINS: {
      if (this->xover_pending_) {
        // settings changed while applying, start again without releasing the mute
        this->xover_pending_ = false;
        this->xover_step_ = XO_WRITE_MODE;
        return true;
      }

      this->xover_applied_ = true;
      bool ok = this->write_channel_gain_(LEFT_CHANNEL) && this->write_channel_gain_(RIGHT_CHANNEL);

      if (ok && this->tweeter_must_be_muted_()) {
        // EQ Off: confirm the tweeter channel gain reads back as mute before releasing the safety mute
        uint8_t gain[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        ok = this->book_and_page_block_(TAS58XX_AUDIO_CTRL_BOOK, TAS58XX_CHANNEL_VOLUME_PAGE,
                                        TAS58XX_CHANNEL_VOLUME_SUBADDR[this->crossover_channel_(crossover::ROLE_TWEETER)],
                                        gain, sizeof(gain), true);
        ok = ok && (gain[0] == 0) && (gain[1] == 0) && (gain[2] == 0) && (gain[3] == 0);
      }

      if (!ok) {
        this->crossover_step_failed_("channel gain write");
        return true;
      }
      this->xover_step_ = XO_UNMUTE;
      return true;
    }

    case XO_UNMUTE:
      if (this->xover_pending_) {
        this->xover_pending_ = false;
        this->xover_applied_ = false;
        this->xover_step_ = XO_WRITE_MODE;
        return true;
      }
      if (!this->set_safety_mute_(false)) {
        ESP_LOGW(TAG, "%s %s releasing safety mute", ERROR, CROSSOVER);
        return true; // try again next loop
      }
      this->xover_failures_ = 0;
      this->xover_step_ = XO_IDLE;
      ESP_LOGI(TAG, "%s applied: %s %.0fHz, tweeter on %s channel%s", CROSSOVER, FILTER_TEXT[this->xover_.filter],
               crossover::effective_frequency(this->xover_), LR_CHANNEL_TEXT[this->xover_tweeter_channel_],
               (this->tas58xx_eq_mode_ == EQ_OFF) ? " (EQ Off: tweeter muted)" : "");
      // pick up any volume change made while applying
      this->update_loudness_();
      return this->xover_pending_;
  }
  return false;
}

void Tas58xxComponent::crossover_step_failed_(const char* what) {
  this->xover_applied_ = false;
  this->xover_failures_++;
  ESP_LOGW(TAG, "%s %s failed, DAC stays muted, retry %d", CROSSOVER, what, this->xover_failures_);
  this->xover_clock_good_reads_ = 0;
  this->xover_wait_loops_ = RETRY_INTERVAL;
  this->xover_step_ = XO_WAIT_CLOCK;
}

bool Tas58xxComponent::wait_for_stable_clock_() {
  if (this->xover_wait_loops_ > 0) {
    this->xover_wait_loops_--;
    return false;
  }

  const char* waiting_for = nullptr;
  uint8_t fs_mon = TAS58XX_FS_MON_ERROR;

  if (this->tas58xx_control_state_ != CTRL_PLAY) {
    // coefficient writes during deep sleep are not reliable, wait for 'enable_dac'
    waiting_for = "DAC to be enabled";
  } else if (!this->tas58xx_read_bytes_(TAS58XX_FS_MON, &fs_mon, 1) || ((fs_mon & TAS58XX_FS_MON_MASK) == TAS58XX_FS_MON_ERROR)) {
    waiting_for = "I2S clock";
  }

  if (waiting_for != nullptr) {
    this->xover_clock_good_reads_ = 0;
    this->xover_wait_loops_ = CLOCK_POLL_INTERVAL;
    const uint32_t now = millis();
    if ((now - this->xover_last_wait_log_) > WAIT_LOG_INTERVAL) {
      ESP_LOGI(TAG, "%s waiting for %s, DAC muted", CROSSOVER, waiting_for);
      this->xover_last_wait_log_ = now;
    }
    return false;
  }

  if (++this->xover_clock_good_reads_ < CLOCK_STABLE_READS) {
    this->xover_wait_loops_ = CLOCK_READ_INTERVAL;
    return false;
  }

  this->xover_clock_good_reads_ = 0;
  if ((fs_mon & TAS58XX_FS_MON_MASK) != TAS58XX_FS_MON_48K) {
    ESP_LOGW(TAG, "%s I2S sample rate code 0x%02X is not 48kHz", CROSSOVER, fs_mon & TAS58XX_FS_MON_MASK);
  }
  return true;
}

bool Tas58xxComponent::set_safety_mute_(bool mute) {
  this->safety_mute_ = mute;
  if (this->tas58xx_write_byte_(TAS58XX_DEVICE_CTRL_2, this->tas58xx_control_state_ + this->ctrl2_mute_bits_())) {
    ESP_LOGV(TAG, "Safety Mute >> %s", ONOFF(mute));
    return true;
  }
  // write failed so the DAC state is unknown, keep treating it as muted
  this->safety_mute_ = true;
  return false;
}

void Tas58xxComponent::encode_crossover_chain_(crossover::DriverRole role, uint8_t* data) {
  crossover::encode_chain(this->xover_, role, this->xover_dsp_sample_rate_, this->xover_loudness_applied_, data);
}

bool Tas58xxComponent::write_crossover_chain_(crossover::DriverRole role) {
  uint8_t data[CHAIN_BYTES];
  this->encode_crossover_chain_(role, data);

  const Channels channel = this->crossover_channel_(role);
  const AddressSequence* start = eq_address(channel, 0);
  if (!this->book_and_page_block_(TAS58XX_EQ_CTRL_BOOK, start->page, start->sub_addr, data, CHAIN_BYTES, false)) return false;

  ESP_LOGD(TAG, "%s %s biquads written to %s channel", CROSSOVER, ROLE_TEXT[role], LR_CHANNEL_TEXT[channel]);
  return true;
}

bool Tas58xxComponent::verify_crossover_chain_(crossover::DriverRole role) {
  uint8_t expected[CHAIN_BYTES];
  uint8_t actual[CHAIN_BYTES];
  this->encode_crossover_chain_(role, expected);

  const Channels channel = this->crossover_channel_(role);
  const AddressSequence* start = eq_address(channel, 0);
  if (!this->book_and_page_block_(TAS58XX_EQ_CTRL_BOOK, start->page, start->sub_addr, actual, CHAIN_BYTES, true)) return false;

  for (uint8_t band = 0; band < crossover::NUMBER_BANDS; band++) {
    if (memcmp(expected + band * BIQUAD_SIZE, actual + band * BIQUAD_SIZE, BIQUAD_SIZE) != 0) {
      ESP_LOGD(TAG, "%s %s band %d readback differs on %s channel", CROSSOVER, ROLE_TEXT[role], band + 1, LR_CHANNEL_TEXT[channel]);
      return false;
    }
  }
  return true;
}

bool Tas58xxComponent::write_crossover_band_(crossover::DriverRole role, uint8_t band) {
  uint8_t data[BIQUAD_SIZE];
  biquad::encode(crossover::band_coefficients(this->xover_, role, band, this->xover_dsp_sample_rate_, this->xover_loudness_applied_), data);

  const AddressSequence* address = eq_address(this->crossover_channel_(role), band);
  return this->book_and_page_write_(TAS58XX_EQ_CTRL_BOOK, address->page, address->sub_addr, data, BIQUAD_SIZE);
}

void Tas58xxComponent::crossover_band_changed_(crossover::DriverRole role, uint8_t band) {
  // an apply in progress may already have written this band, so start it again
  if (this->xover_step_ != XO_IDLE) {
    this->request_crossover_apply_(false);
    return;
  }
  // not applied yet: the first apply will include the new value
  if (!this->xover_applied_) return;

  // gain, delay and PEQ bands cannot remove the tweeter high-pass, so write them while playing
  // with headroom compensation the gain trim follows the filter boost, so write it as well
  bool ok = this->write_crossover_band_(role, band);
  if (ok && this->xover_.headroom_compensation && band != crossover::BAND_GAIN) {
    ok = this->write_crossover_band_(role, crossover::BAND_GAIN);
  }
  if (!ok) {
    ESP_LOGW(TAG, "%s writing %s band %d, reapplying", ERROR, ROLE_TEXT[role], band + 1);
    this->request_crossover_apply_(false);
  }
}

void Tas58xxComponent::write_gain_bands_if_compensated_() {
  if (!this->xover_.headroom_compensation) return;
  if (!this->write_crossover_band_(crossover::ROLE_TWEETER, crossover::BAND_GAIN) ||
      !this->write_crossover_band_(crossover::ROLE_MID, crossover::BAND_GAIN)) {
    ESP_LOGW(TAG, "%s writing gain trim, reapplying", ERROR);
    this->request_crossover_apply_(false);
  }
}

float Tas58xxComponent::get_crossover_peak_db(uint8_t role) {
  if (role >= crossover::NUMBER_ROLES) return 0.0f;
  // the search costs a few thousand complex evaluations, only redo it after a change
  if (!this->xover_peak_valid_) {
    for (uint8_t r = 0; r < crossover::NUMBER_ROLES; r++) {
      this->xover_peak_db_[r] = crossover::chain_peak_db(this->xover_, static_cast<crossover::DriverRole>(r),
                                                         this->xover_dsp_sample_rate_, this->xover_loudness_applied_, true);
    }
    this->xover_peak_valid_ = true;
  }
  return this->xover_peak_db_[role];
}

void Tas58xxComponent::reset_crossover_settings() {
  ESP_LOGI(TAG, "%s settings reset to YAML values", CROSSOVER);
  this->xover_ = this->xover_defaults_;
  this->xover_peak_valid_ = false;
  this->xover_tweeter_channel_ = this->xover_default_tweeter_channel_;
  // entities show and save the YAML values again
  for (auto &listener : this->xover_listeners_) listener();
  // tweeter channel may have changed, so rewrite everything under the safety mute
  this->request_crossover_apply_(false);
}

void Tas58xxComponent::export_crossover_settings() {
  // printed as a crossover: block that can be pasted into another cabinet's YAML
  static constexpr const char* FILTER_YAML[crossover::NUMBER_FILTERS] = {"LR2", "LR4", "BW2", "BW4"};
  static constexpr const char* PEQ_YAML[crossover::NUMBER_PEQ_TYPES] = {"PEAKING", "LOW_SHELF", "HIGH_SHELF"};
  static constexpr const char* PLACEMENT_YAML[crossover::NUMBER_PLACEMENTS] = {"FREESTANDING", "NEAR_WALL", "CORNER"};
  const crossover::Settings &s = this->xover_;
  const crossover::DriverSettings &tw = s.driver[crossover::ROLE_TWEETER];
  const crossover::DriverSettings &mid = s.driver[crossover::ROLE_MID];

  ESP_LOGI(TAG, "%s export, paste under audio_dac: platform: tas58xx", CROSSOVER);
  ESP_LOGI(TAG, "    crossover:");
  ESP_LOGI(TAG, "      filter: %s", FILTER_YAML[s.filter]);
  ESP_LOGI(TAG, "      frequency: %.0fHz", s.frequency);
  ESP_LOGI(TAG, "      tweeter_min_frequency: %.0fHz", s.frequency_min);
  ESP_LOGI(TAG, "      tweeter_channel: %s", this->xover_tweeter_channel_ == LEFT_CHANNEL ? "LEFT" : "RIGHT");
  ESP_LOGI(TAG, "      dsp_sample_rate: %.0fHz", this->xover_dsp_sample_rate_);
  ESP_LOGI(TAG, "      tweeter_gain: %.1fdB", tw.gain_db);
  ESP_LOGI(TAG, "      mid_gain: %.1fdB", mid.gain_db);
  ESP_LOGI(TAG, "      tweeter_invert: %s", TRUEFALSE(tw.invert));
  ESP_LOGI(TAG, "      mid_invert: %s", TRUEFALSE(mid.invert));
  ESP_LOGI(TAG, "      tweeter_delay: %.0f", tw.delay_mm);
  ESP_LOGI(TAG, "      mid_delay: %.0f", mid.delay_mm);
  const crossover::DriverSettings* drivers[crossover::NUMBER_ROLES] = {&tw, &mid};
  const char* peq_keys[crossover::NUMBER_ROLES] = {"tweeter_peq", "mid_peq"};
  for (uint8_t role = 0; role < crossover::NUMBER_ROLES; role++) {
    ESP_LOGI(TAG, "      %s:", peq_keys[role]);
    for (uint8_t band = 0; band < crossover::NUMBER_PEQ_BANDS; band++) {
      const crossover::PeqBand &peq = drivers[role]->peq[band];
      ESP_LOGI(TAG, "        - {frequency: %.0fHz, gain: %.1fdB, q: %.2f, type: %s}", peq.frequency, peq.gain_db, peq.q, PEQ_YAML[peq.type]);
    }
  }
  ESP_LOGI(TAG, "      subsonic_frequency: %.0fHz", s.subsonic_frequency);
  ESP_LOGI(TAG, "      baffle_width: %.0f", s.baffle_width_cm);
  ESP_LOGI(TAG, "      baffle_placement: %s", PLACEMENT_YAML[s.baffle_placement]);
  ESP_LOGI(TAG, "      tweeter_notch: {frequency: %.0fHz, gain: %.1fdB, q: %.1f}", s.notch_frequency, s.notch_gain_db, s.notch_q);
  ESP_LOGI(TAG, "      air_gain: %.1fdB", s.air_gain_db);
  ESP_LOGI(TAG, "      linkwitz: {f0: %.0fHz, q0: %.2f, fp: %.0fHz, qp: %.2f}", s.linkwitz_f0, s.linkwitz_q0, s.linkwitz_fp, s.linkwitz_qp);
  ESP_LOGI(TAG, "      headroom_compensation: %s", TRUEFALSE(s.headroom_compensation));
  ESP_LOGI(TAG, "      loudness: %s", TRUEFALSE(s.loudness));
  ESP_LOGI(TAG, "      loudness_reference: %.0fdB", s.loudness_reference_db);
}

void Tas58xxComponent::crossover_sections_changed_() {
  if (this->xover_step_ != XO_IDLE) {
    this->request_crossover_apply_(false);
    return;
  }
  if (!this->xover_applied_) return;

  // filter type and frequency changes keep the tweeter on a high-pass at every intermediate step,
  // a failed write could leave a partial biquad so reapply under the safety mute
  for (uint8_t role = 0; role < crossover::NUMBER_ROLES; role++) {
    for (uint8_t i = 0; i < crossover::BAND_CROSSOVER_COUNT; i++) {
      if (!this->write_crossover_band_(static_cast<crossover::DriverRole>(role), crossover::BAND_CROSSOVER_START + i)) {
        ESP_LOGW(TAG, "%s writing %s sections, reapplying", ERROR, CROSSOVER);
        this->request_crossover_apply_(false);
        return;
      }
    }
  }
  this->write_gain_bands_if_compensated_();
}

void Tas58xxComponent::update_loudness_() {
  // an apply in progress calls this again when it finishes
  if (this->xover_step_ != XO_IDLE || !this->xover_applied_) return;

  const crossover::LoudnessBoost boost = crossover::loudness_boost(this->xover_, this->xover_volume_db_);
  if ((boost.bass_db == this->xover_loudness_applied_.bass_db) && (boost.treble_db == this->xover_loudness_applied_.treble_db)) return;

  this->xover_loudness_applied_ = boost;
  this->xover_peak_valid_ = false;
  ESP_LOGD(TAG, "Loudness at %.1fdB: Bass +%.1fdB Treble +%.1fdB", this->xover_volume_db_, boost.bass_db, boost.treble_db);

  if (!this->write_crossover_band_(crossover::ROLE_MID, crossover::BAND_LOUDNESS_BASS) ||
      !this->write_crossover_band_(crossover::ROLE_TWEETER, crossover::BAND_LOUDNESS_TREBLE)) {
    ESP_LOGW(TAG, "%s writing Loudness, reapplying", ERROR);
    this->request_crossover_apply_(false);
    return;
  }
  this->write_gain_bands_if_compensated_();
}

bool Tas58xxComponent::book_and_page_block_(uint8_t book, uint8_t page, uint8_t sub_addr, uint8_t* data, uint16_t number_bytes, bool read) {
  // read or write consecutive registers spanning several pages, with the book held selected for the whole sequence
  // each page holds registers 0x08 to 0x7F, consecutive biquads continue at 0x08 on the next page
  static constexpr uint8_t PAGE_SIZE = 0x80;
  static constexpr uint8_t MINIMUM_PAGE_SUBADDR = 0x08;

  if (sub_addr < MINIMUM_PAGE_SUBADDR || sub_addr >= PAGE_SIZE || number_bytes == 0) {
    ESP_LOGE(TAG, "Incorrect address for book and page block");
    return false;
  }

  bool ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, TAS58XX_PAGE_ZERO);
  if (ok) ok = this->tas58xx_write_byte_(TAS58XX_BOOK_SET, book);
  if (ok) ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, page);

  while (ok && number_bytes > 0) {
    const uint8_t chunk = (number_bytes < (PAGE_SIZE - sub_addr)) ? number_bytes : (PAGE_SIZE - sub_addr);
    ESP_LOGV(TAG, "%s book:0x%02X page:0x%02X subaddress:0x%02X bytes:%d", read ? "Reading" : "Writing", book, page, sub_addr, chunk);
    ok = read ? this->tas58xx_read_bytes_(sub_addr, data, chunk) : this->tas58xx_write_bytes_(sub_addr, data, chunk);
    data += chunk;
    number_bytes -= chunk;
    if (ok && number_bytes > 0) {
      page++;
      sub_addr = MINIMUM_PAGE_SUBADDR;
      ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, page);
    }
  }

  // always return to book and page zero, also after an error
  bool reset_ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, TAS58XX_PAGE_ZERO);
  if (reset_ok) reset_ok = this->tas58xx_write_byte_(TAS58XX_BOOK_SET, TAS58XX_BOOK_ZERO);
  if (reset_ok) reset_ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, TAS58XX_PAGE_ZERO);
  if (!reset_ok) ESP_LOGE(TAG, "%s re-setting book and page", ERROR);

  return ok && reset_ok;
}
#endif // USE_TAS58XX_EQ_CROSSOVER


//// low level functions

bool Tas58xxComponent::book_and_page_write_(uint8_t book, uint8_t page, uint8_t sub_addr, uint8_t* data, uint8_t number_bytes) {
  // write up to 20 bytes (BIQUAD_SIZE) to a book and page starting at subaddress
  // limited to writing across one page boundary as is required for tas5805m while tas5825m has biquads aligned to page boundaries
  // shorter consecutive writes required by tas5805m and tas5825m do not extend over page boundaries

  // Biquad addressing constants
  static constexpr uint8_t PAGE_SIZE = 0x80;           		// 0x7F + 1 = 0x80
  static constexpr uint8_t MINIMUM_PAGE_SUBADDR = 0x08;   // start subaddr for pages = 0x08

  if (number_bytes == 0 || number_bytes > BIQUAD_SIZE) {
    ESP_LOGE(TAG, "Incorrect length for book and page write");
    return false;
  }

  uint8_t bytes_in_block1{number_bytes};
  uint8_t bytes_in_block2{0};

  if ((sub_addr + number_bytes) > PAGE_SIZE) {
    bytes_in_block1 = PAGE_SIZE - sub_addr;
    bytes_in_block2 = number_bytes - bytes_in_block1;
  }

  // set book and page
  bool ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, TAS58XX_PAGE_ZERO);
  if (ok) ok = this->tas58xx_write_byte_(TAS58XX_BOOK_SET, book);
  if (ok) ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, page);
  if (!ok) {
    ESP_LOGE(TAG, "%s setting book:0x%02X page:0x%02X", ERROR, book, page);
    return false;
  }

  // do block write to book and page sub-address
  ESP_LOGV(TAG, "Writing book:0x%02X page:0x%02X subaddress:0x%02X bytes:%d", book, page, sub_addr, bytes_in_block1);
  if (!this->tas58xx_write_bytes_(sub_addr, data, bytes_in_block1)) return false;

  if (bytes_in_block2 != 0) {
    uint8_t next_page = page + 1;

    // book already set so just change to next page
    if (!this->tas58xx_write_byte_(TAS58XX_PAGE_SET, next_page)) {
      ESP_LOGE(TAG, "%s setting next page", ERROR);
      return false;
    }
    ESP_LOGV(TAG, "Writing book:0x%02X page:0x%02X subaddress:0x%02X bytes:%d", book, next_page, MINIMUM_PAGE_SUBADDR, bytes_in_block2);
    if (!this->tas58xx_write_bytes_(MINIMUM_PAGE_SUBADDR, data + bytes_in_block1, bytes_in_block2)) return false;
  }

  // reset book and page to zero
  ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, TAS58XX_PAGE_ZERO);
  if (ok) ok = this->tas58xx_write_byte_(TAS58XX_BOOK_SET, TAS58XX_BOOK_ZERO);
  if (ok) ok = this->tas58xx_write_byte_(TAS58XX_PAGE_SET, TAS58XX_PAGE_ZERO);
  if (!ok) {
    ESP_LOGE(TAG, "%s re-setting book and page", ERROR);
    return false;
  }
  return true;
}

bool Tas58xxComponent::tas58xx_read_bytes_(uint8_t a_register, uint8_t* data, uint8_t number_bytes) {
  i2c::ErrorCode error_code;
  error_code = this->write(&a_register, 1);
  if (error_code != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "%s code:%d writing address:0x%02X to start read", ERROR, error_code, a_register);
    this->i2c_error_ = (uint8_t)error_code;
    return false;
  }
  error_code = this->read_register(a_register, data, number_bytes);
  if (error_code != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "%s code:%d reading %d bytes from address:0x%02X", ERROR, error_code, number_bytes, a_register);
    this->i2c_error_ = (uint8_t)error_code;
    return false;
  }
  return true;
}

bool Tas58xxComponent::tas58xx_write_byte_(uint8_t a_register, uint8_t data) {
   i2c::ErrorCode error_code = this->write_register(a_register, &data, 1);
  if (error_code != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "%s code:%d writing to address:0x%02X", ERROR, error_code, a_register);
    this->i2c_error_ = (uint8_t)error_code;
    return false;
  }
  return true;
}

bool Tas58xxComponent::tas58xx_write_bytes_(uint8_t a_register, uint8_t* data, uint8_t number_bytes) {
  i2c::ErrorCode error_code = this->write_register(a_register, data, number_bytes);
  if (error_code != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "%s code:%d writing %d bytes to address:0x%02X", ERROR, error_code, number_bytes, a_register);
    this->i2c_error_ = (uint8_t)error_code;
    return false;
  }
  return true;
}

}  // namespace esphome::tas58xx
