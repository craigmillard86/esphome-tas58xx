#pragma once

// Two-way active crossover built from the 15 biquads per channel of the TAS58xx.
// Plain C++ with no ESPHome dependencies so it can be unit tested on a host.
//
// Band allocation per driver (same layout as the snapclient bi-amp port):
//   band 0      gain trim + polarity
//   band 1      delay (1st order all-pass approximation)
//   bands 2-5   crossover sections (high-pass for tweeter, low-pass for mid), unused ones passthrough
//   bands 6-11  parametric EQ, 6 bands
//   band 12     spare, passthrough
//   band 13     mid: loudness bass shelf     tweeter: passthrough
//   band 14     tweeter: loudness treble shelf  mid: passthrough

#include <cstdint>

#include "tas58xx_biquad.h"

namespace esphome::tas58xx::crossover {

enum CrossoverFilter : uint8_t {
  FILTER_LR2 = 0,  // Linkwitz-Riley 2nd order, 12dB/oct, needs one driver inverted
  FILTER_LR4,      // Linkwitz-Riley 4th order, 24dB/oct
  FILTER_BW2,      // Butterworth 2nd order, 12dB/oct
  FILTER_BW4,      // Butterworth 4th order, 24dB/oct
};

enum PeqType : uint8_t {
  PEQ_PEAKING = 0,
  PEQ_LOW_SHELF,
  PEQ_HIGH_SHELF,
};

enum DriverRole : uint8_t {
  ROLE_TWEETER = 0,
  ROLE_MID,
};

static constexpr uint8_t NUMBER_ROLES = 2;
static constexpr uint8_t NUMBER_BANDS = 15;
static constexpr uint8_t NUMBER_PEQ_BANDS = 6;
static constexpr uint8_t NUMBER_FILTERS = 4;
static constexpr uint8_t NUMBER_PEQ_TYPES = 3;

static constexpr uint8_t BAND_GAIN = 0;
static constexpr uint8_t BAND_DELAY = 1;
static constexpr uint8_t BAND_CROSSOVER_START = 2;
static constexpr uint8_t BAND_CROSSOVER_COUNT = 4;
static constexpr uint8_t BAND_PEQ_START = 6;
static constexpr uint8_t BAND_LOUDNESS_BASS = 13;    // on mid driver
static constexpr uint8_t BAND_LOUDNESS_TREBLE = 14;  // on tweeter driver

static constexpr float FREQUENCY_MIN = 500.0f;
static constexpr float FREQUENCY_MAX = 8000.0f;
static constexpr float DRIVER_GAIN_MIN_DB = -24.0f;
static constexpr float DRIVER_GAIN_MAX_DB = 6.0f;
static constexpr float DELAY_MAX_MM = 50.0f;
static constexpr float PEQ_FREQUENCY_MIN = 20.0f;
static constexpr float PEQ_FREQUENCY_MAX = 20000.0f;
static constexpr float PEQ_GAIN_MAX_DB = 15.0f;
static constexpr float PEQ_Q_MIN = 0.1f;
static constexpr float PEQ_Q_MAX = 10.0f;
static constexpr float LOUDNESS_REFERENCE_MIN_DB = -60.0f;
static constexpr float LOUDNESS_REFERENCE_MAX_DB = 0.0f;

// loudness: boost grows linearly with attenuation below the reference level
// maximum boost values follow the snapclient defaults for its lowest volume zone
static constexpr float LOUDNESS_BASS_FREQUENCY = 200.0f;
static constexpr float LOUDNESS_TREBLE_FREQUENCY = 4000.0f;
static constexpr float LOUDNESS_BASS_MAX_DB = 6.0f;
static constexpr float LOUDNESS_TREBLE_MAX_DB = 4.0f;
static constexpr float LOUDNESS_FULL_BOOST_ATTENUATION_DB = 30.0f;
static constexpr float LOUDNESS_STEP_DB = 0.5f;

struct PeqBand {
  float frequency{1000.0f};
  float gain_db{0.0f};
  float q{1.0f};
  PeqType type{PEQ_PEAKING};
};

struct DriverSettings {
  float gain_db{0.0f};
  bool invert{false};
  float delay_mm{0.0f};
  PeqBand peq[NUMBER_PEQ_BANDS];
};

struct Settings {
  CrossoverFilter filter{FILTER_LR4};
  float frequency{2500.0f};
  DriverSettings driver[NUMBER_ROLES];
  bool loudness{false};
  float loudness_reference_db{0.0f};
};

struct LoudnessBoost {
  float bass_db{0.0f};
  float treble_db{0.0f};
};

// number of 2nd order sections used by a filter type (1 or 2)
uint8_t crossover_sections(CrossoverFilter filter);

// coefficients of one crossover section, index < crossover_sections()
biquad::Coefficients crossover_section(CrossoverFilter filter, bool highpass, uint8_t index, float frequency, float fs);

// loudness boost at a digital volume, quantised to LOUDNESS_STEP_DB
LoudnessBoost loudness_boost(const Settings &settings, float volume_db);

// coefficients for one band of one driver
biquad::Coefficients band_coefficients(const Settings &settings, DriverRole role, uint8_t band, float fs,
                                       const LoudnessBoost &loudness);

// encoded coefficients for all 15 bands of one driver, in register order
void encode_chain(const Settings &settings, DriverRole role, float fs, const LoudnessBoost &loudness,
                  uint8_t out[NUMBER_BANDS * biquad::ENCODED_SIZE]);

}  // namespace esphome::tas58xx::crossover
