#pragma once

namespace esphome::tas58xx {

enum ControlState : uint8_t {
    CTRL_DEEP_SLEEP = 0x00, // Deep Sleep
    CTRL_SLEEP      = 0x01, // Sleep
    CTRL_HI_Z       = 0x02, // Hi-Z
    CTRL_PLAY       = 0x03, // Play
   };

enum DacMode : uint8_t {
  BTL  = 0, // Bridge tied load
  PBTL = 1, // Parallel load
};

enum ModulationScheme : uint8_t {
  MODE_BD = 0,
  MODE_1SPW = 1,
};

enum EqRefreshMode : uint8_t {
    AUTO   = 0,
    MANUAL = 1,
};

enum ExcludeIgnoreMode : uint8_t {
    NONE        = 0,
    CLOCK_FAULT = 1,
};

enum LoopSetupStage : uint8_t {
    WAIT_FOR_TRIGGER = 0,
    RUN_DELAY_LOOP,
    WAIT_FOR_CLOCK,     // crossover only: wait until the DAC reports a valid I2S clock
    INPUT_MIXER_SETUP,
    LR_VOLUME_SETUP,
    EQ_BANDS_SETUP,
    EQ_PRESETS_SETUP,
    CROSSOVER_SETUP,
    SETUP_COMPLETE,
};

// parameters set by crossover numbers, selects and switches
enum CrossoverParam : uint8_t {
    XO_PARAM_FILTER = 0,
    XO_PARAM_TWEETER_CHANNEL,
    XO_PARAM_FREQUENCY,
    XO_PARAM_GAIN,
    XO_PARAM_INVERT,
    XO_PARAM_DELAY,
    XO_PARAM_LOUDNESS,
    XO_PARAM_LOUDNESS_REFERENCE,
    XO_PARAM_PEQ_FREQUENCY,
    XO_PARAM_PEQ_GAIN,
    XO_PARAM_PEQ_Q,
    XO_PARAM_PEQ_TYPE,
    XO_PARAM_SUBSONIC_FREQUENCY,
    XO_PARAM_BAFFLE_WIDTH,
    XO_PARAM_BAFFLE_PLACEMENT,
    XO_PARAM_NOTCH_FREQUENCY,
    XO_PARAM_NOTCH_GAIN,
    XO_PARAM_NOTCH_Q,
    XO_PARAM_AIR_GAIN,
};

// steps used by 'loop' to (re)apply the crossover while the DAC is muted
enum CrossoverStep : uint8_t {
    XO_IDLE = 0,
    XO_WAIT_CLOCK,
    XO_WRITE_MODE,
    XO_WRITE_TWEETER,
    XO_WRITE_MID,
    XO_VERIFY,
    XO_WRITE_GAINS,
    XO_UNMUTE,
};
struct Tas58xxFault {
  uint8_t channel_fault{0};                  // individual faults extracted when publishing
  uint8_t global_fault{0};                   // individual faults extracted when publishing

  bool clock_fault{false};
  bool temperature_fault{false};
  bool temperature_warning{false};

  bool is_fault_except_clock_fault{false};   // fault conditions combined except clock fault

#ifdef USE_TAS58XX_BINARY_SENSOR
  bool have_fault{false};                    // combined binary sensor - any fault found but does not include clock fault if excluded
#endif
};

static constexpr float TAS58XX_MIN_ANALOG_GAIN         = -15.5;
static constexpr float TAS58XX_MAX_ANALOG_GAIN         = 0.0;

// set book and page registers
static constexpr uint8_t TAS58XX_PAGE_SET              = 0x00;
static constexpr uint8_t TAS58XX_BOOK_SET              = 0x7F;
static constexpr uint8_t TAS58XX_BOOK_ZERO             = 0x00;
static constexpr uint8_t TAS58XX_PAGE_ZERO             = 0x00;

// tas58x5m registers
static constexpr uint8_t TAS58XX_DEVICE_CTRL_1         = 0x02;
static constexpr uint8_t TAS58XX_DEVICE_CTRL_2         = 0x03;
static constexpr uint8_t TAS58XX_FS_MON                = 0x37;
static constexpr uint8_t TAS58XX_FS_MON_MASK           = 0x0F; // detected sample rate bits
static constexpr uint8_t TAS58XX_FS_MON_ERROR          = 0x00; // no valid I2S clock
static constexpr uint8_t TAS58XX_FS_MON_48K            = 0x09;
static constexpr uint8_t TAS58XX_BCK_MON               = 0x38;
static constexpr uint8_t TAS58XX_DIG_VOL_CTRL          = 0x4C;
static constexpr uint8_t TAS58XX_ANA_CTRL              = 0x53;
static constexpr uint8_t TAS58XX_AGAIN                 = 0x54;
static constexpr uint8_t TAS58XX_POWER_STATE           = 0x68;

// TAS58XX FAULT constants
static constexpr uint8_t TAS58XX_CHAN_FAULT            = 0x70;
static constexpr uint8_t TAS58XX_GLOBAL_FAULT1         = 0x71;
static constexpr uint8_t TAS58XX_GLOBAL_FAULT2         = 0x72;
static constexpr uint8_t TAS58XX_OT_WARNING            = 0x73;
static constexpr uint8_t TAS58XX_FAULT_CLEAR           = 0x78;
static constexpr uint8_t TAS58XX_ANALOG_FAULT_CLEAR    = 0x80;

}  // namespace esphome::tas58xx
