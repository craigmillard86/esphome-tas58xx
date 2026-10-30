#pragma once

// Two-way active crossover built from the 15 biquads per channel of the TAS58xx.
// Plain C++ with no ESPHome dependencies so it can be unit tested on a host.
//
// Band allocation per driver (same layout as the snapclient bi-amp port):
//   band 0      gain trim + polarity
//   band 1      delay (1st order all-pass approximation)
//   bands 2-3   crossover sections (high-pass for tweeter, low-pass for mid), unused ones passthrough
//   band 4      mid: subsonic high-pass       tweeter: passthrough
//   band 5      mid: Linkwitz transform       tweeter: passthrough
//   bands 6-11  parametric EQ, 6 bands
//   band 12     mid: baffle step low shelf    tweeter: breakup notch
//   band 13     mid: loudness bass shelf      tweeter: air high shelf
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

enum BafflePlacement : uint8_t {
  PLACEMENT_FREESTANDING = 0,  // +6dB, no boundary reinforcement
  PLACEMENT_NEAR_WALL,         // +3dB, wall gives partial reinforcement
  PLACEMENT_CORNER,            // 0dB, corner gives full reinforcement
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
static constexpr uint8_t NUMBER_PLACEMENTS = 3;

static constexpr uint8_t BAND_GAIN = 0;
static constexpr uint8_t BAND_DELAY = 1;
static constexpr uint8_t BAND_CROSSOVER_START = 2;
static constexpr uint8_t BAND_CROSSOVER_COUNT = 2;  // LR4 and BW4 need 2 sections
static constexpr uint8_t BAND_SUBSONIC = 4;          // on mid driver
static constexpr uint8_t BAND_LINKWITZ = 5;          // on mid driver
static constexpr uint8_t BAND_PEQ_START = 6;
static constexpr uint8_t BAND_BAFFLE_STEP = 12;      // on mid driver
static constexpr uint8_t BAND_NOTCH = 12;            // on tweeter driver
static constexpr uint8_t BAND_LOUDNESS_BASS = 13;    // on mid driver
static constexpr uint8_t BAND_AIR = 13;              // on tweeter driver
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

// subsonic high-pass on the mid driver, 2nd order Butterworth, below the minimum it is off
static constexpr float SUBSONIC_FREQUENCY_MIN = 10.0f;
static constexpr float SUBSONIC_FREQUENCY_MAX = 100.0f;

// baffle step: low shelf at f = c / (pi * width), gain set by room placement, width 0 is off
static constexpr float BAFFLE_WIDTH_MIN_CM = 5.0f;
static constexpr float BAFFLE_WIDTH_MAX_CM = 50.0f;
static constexpr float SPEED_OF_SOUND_M_PER_S = 343.0f;

// tweeter breakup notch, only a cut: frequency 0 or gain 0 is off
static constexpr float NOTCH_FREQUENCY_MIN = 1000.0f;
static constexpr float NOTCH_FREQUENCY_MAX = 20000.0f;
static constexpr float NOTCH_GAIN_MIN_DB = -24.0f;
static constexpr float NOTCH_Q_MIN = 0.5f;
static constexpr float NOTCH_Q_MAX = 20.0f;

// Linkwitz transform on the mid driver, f0 or fp 0 is off
static constexpr float LINKWITZ_FREQUENCY_MIN = 15.0f;
static constexpr float LINKWITZ_FREQUENCY_MAX = 200.0f;
static constexpr float LINKWITZ_Q_MIN = 0.3f;
static constexpr float LINKWITZ_Q_MAX = 2.0f;

// peak gain is searched from 5Hz to 20kHz (or just below Nyquist) in 1/24 octave steps
// starting below 20Hz finds the Linkwitz transform boost, which keeps rising towards DC
static constexpr float PEAK_SEARCH_START = 5.0f;
static constexpr float PEAK_SEARCH_STOP = 20000.0f;
static constexpr float PEAK_SEARCH_STEP = 1.0293022366f;  // 2^(1/24)

// air / brilliance high shelf on the tweeter at a fixed 10kHz corner
static constexpr float AIR_FREQUENCY = 10000.0f;
static constexpr float AIR_GAIN_MAX_DB = 6.0f;

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
  float frequency_min{FREQUENCY_MIN};  // YAML limit to protect the tweeter, crossover never goes below it
  DriverSettings driver[NUMBER_ROLES];
  bool loudness{false};
  float loudness_reference_db{0.0f};

  float subsonic_frequency{0.0f};  // mid, 0 is off
  float baffle_width_cm{0.0f};     // mid, 0 is off
  BafflePlacement baffle_placement{PLACEMENT_FREESTANDING};
  float notch_frequency{0.0f};     // tweeter, 0 is off
  float notch_gain_db{0.0f};
  float notch_q{5.0f};
  float air_gain_db{0.0f};         // tweeter, 0 is off

  float linkwitz_f0{0.0f};         // mid, measured box resonance, 0 is off
  float linkwitz_q0{0.707f};
  float linkwitz_fp{0.0f};         // mid, target resonance, 0 is off
  float linkwitz_qp{0.5f};

  // lower each driver's gain trim by the peak boost of its filters so the DSP cannot clip
  bool headroom_compensation{false};
};

struct BaffleStep {
  float frequency{0.0f};
  float gain_db{0.0f};
};

struct LoudnessBoost {
  float bass_db{0.0f};
  float treble_db{0.0f};
};

// number of 2nd order sections used by a filter type (1 or 2)
uint8_t crossover_sections(CrossoverFilter filter);

// coefficients of one crossover section, index < crossover_sections()
biquad::Coefficients crossover_section(CrossoverFilter filter, bool highpass, uint8_t index, float frequency, float fs);

// baffle step shelf frequency and gain, gain 0 when off
BaffleStep baffle_step(float width_cm, BafflePlacement placement);

// loudness boost at a digital volume, quantised to LOUDNESS_STEP_DB
LoudnessBoost loudness_boost(const Settings &settings, float volume_db);

// crossover frequency after the YAML minimum and the supported range are applied
float effective_frequency(const Settings &settings);

// highest gain of a driver's chain from 5Hz to 20kHz in dB
// include_gain_band false leaves out band 0 (gain trim), used for headroom compensation
float chain_peak_db(const Settings &settings, DriverRole role, float fs, const LoudnessBoost &loudness, bool include_gain_band);

// gain trim actually written to band 0, after headroom compensation
float effective_gain_db(const Settings &settings, DriverRole role, float fs, const LoudnessBoost &loudness);

// coefficients for one band of one driver
biquad::Coefficients band_coefficients(const Settings &settings, DriverRole role, uint8_t band, float fs,
                                       const LoudnessBoost &loudness);

// encoded coefficients for all 15 bands of one driver, in register order
void encode_chain(const Settings &settings, DriverRole role, float fs, const LoudnessBoost &loudness,
                  uint8_t out[NUMBER_BANDS * biquad::ENCODED_SIZE]);

}  // namespace esphome::tas58xx::crossover
