#include "tas58xx_crossover.h"

#include <cmath>
#include <complex>

namespace esphome::tas58xx::crossover {

static constexpr float PI_F = 3.14159265358979f;

static float clampf(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

uint8_t crossover_sections(CrossoverFilter filter) {
  switch (filter) {
    case FILTER_LR2:
    case FILTER_BW2:
      return 1;
    case FILTER_LR4:
    case FILTER_BW4:
    default:
      return 2;
  }
}

biquad::Coefficients crossover_section(CrossoverFilter filter, bool highpass, uint8_t index, float frequency, float fs) {
  if (index >= crossover_sections(filter)) return biquad::passthrough();

  double q;
  switch (filter) {
    case FILTER_LR2:
      q = biquad::Q_LR2;
      break;
    case FILTER_BW2:
      q = biquad::Q_BUTTERWORTH;
      break;
    case FILTER_BW4:
      q = (index == 0) ? biquad::Q_BW4_STAGE1 : biquad::Q_BW4_STAGE2;
      break;
    case FILTER_LR4:
    default:
      // LR4 is two identical 2nd order Butterworth sections
      q = biquad::Q_BUTTERWORTH;
      break;
  }

  const double fc = clampf(frequency, FREQUENCY_MIN, FREQUENCY_MAX);
  return highpass ? biquad::highpass(fc, q, fs) : biquad::lowpass(fc, q, fs);
}

BaffleStep baffle_step(float width_cm, BafflePlacement placement) {
  BaffleStep step;
  if (width_cm <= 0.0f) return step;

  const float width_m = clampf(width_cm, BAFFLE_WIDTH_MIN_CM, BAFFLE_WIDTH_MAX_CM) / 100.0f;
  step.frequency = SPEED_OF_SOUND_M_PER_S / (PI_F * width_m);
  switch (placement) {
    case PLACEMENT_FREESTANDING:
      step.gain_db = 6.0f;
      break;
    case PLACEMENT_NEAR_WALL:
      step.gain_db = 3.0f;
      break;
    case PLACEMENT_CORNER:
    default:
      step.gain_db = 0.0f;
      break;
  }
  return step;
}

LoudnessBoost loudness_boost(const Settings &settings, float volume_db) {
  LoudnessBoost boost;
  if (!settings.loudness) return boost;

  const float attenuation = settings.loudness_reference_db - volume_db;
  if (attenuation <= 0.0f) return boost;

  const float amount = clampf(attenuation / LOUDNESS_FULL_BOOST_ATTENUATION_DB, 0.0f, 1.0f);
  boost.bass_db = std::round(amount * LOUDNESS_BASS_MAX_DB / LOUDNESS_STEP_DB) * LOUDNESS_STEP_DB;
  boost.treble_db = std::round(amount * LOUDNESS_TREBLE_MAX_DB / LOUDNESS_STEP_DB) * LOUDNESS_STEP_DB;
  return boost;
}

static biquad::Coefficients peq_coefficients(const PeqBand &peq, float fs) {
  const double fc = clampf(peq.frequency, PEQ_FREQUENCY_MIN, PEQ_FREQUENCY_MAX);
  const double gain_db = clampf(peq.gain_db, -PEQ_GAIN_MAX_DB, PEQ_GAIN_MAX_DB);
  const double q = clampf(peq.q, PEQ_Q_MIN, PEQ_Q_MAX);
  switch (peq.type) {
    case PEQ_LOW_SHELF:
      return biquad::low_shelf(fc, gain_db, q, fs);
    case PEQ_HIGH_SHELF:
      return biquad::high_shelf(fc, gain_db, q, fs);
    case PEQ_PEAKING:
    default:
      return biquad::peaking(fc, gain_db, q, fs);
  }
}

float effective_frequency(const Settings &settings) {
  const float minimum = clampf(settings.frequency_min, FREQUENCY_MIN, FREQUENCY_MAX);
  return clampf(settings.frequency, minimum, FREQUENCY_MAX);
}

float chain_peak_db(const Settings &settings, DriverRole role, float fs, const LoudnessBoost &loudness, bool include_gain_band) {
  biquad::Coefficients bands[NUMBER_BANDS];
  for (uint8_t band = 0; band < NUMBER_BANDS; band++) {
    bands[band] = (band == BAND_GAIN && !include_gain_band) ? biquad::passthrough()
                                                             : band_coefficients(settings, role, band, fs, loudness);
  }

  float peak = 0.0f;
  for (float f = PEAK_SEARCH_START; f <= PEAK_SEARCH_STOP; f *= PEAK_SEARCH_STEP) {
    const std::complex<float> z1 = std::polar(1.0f, -2.0f * PI_F * f / fs);
    const std::complex<float> z2 = z1 * z1;
    float magnitude = 1.0f;
    for (const biquad::Coefficients &c : bands) {
      const std::complex<float> num = static_cast<float>(c.b0) + static_cast<float>(c.b1) * z1 + static_cast<float>(c.b2) * z2;
      const std::complex<float> den = 1.0f + static_cast<float>(c.a1) * z1 + static_cast<float>(c.a2) * z2;
      magnitude *= std::abs(num) / std::abs(den);
    }
    if (magnitude > peak) peak = magnitude;
  }
  return 20.0f * std::log10(peak > 1e-9f ? peak : 1e-9f);
}

float effective_gain_db(const Settings &settings, DriverRole role, float fs, const LoudnessBoost &loudness) {
  float gain_db = clampf(settings.driver[role].gain_db, DRIVER_GAIN_MIN_DB, DRIVER_GAIN_MAX_DB);
  if (settings.headroom_compensation) {
    const float boost = chain_peak_db(settings, role, fs, loudness, false);
    if (boost > 0.0f) gain_db -= boost;
  }
  return gain_db;
}

biquad::Coefficients band_coefficients(const Settings &settings, DriverRole role, uint8_t band, float fs,
                                       const LoudnessBoost &loudness) {
  const DriverSettings &driver = settings.driver[role];
  const bool is_tweeter = (role == ROLE_TWEETER);

  if (band == BAND_GAIN) {
    return biquad::gain(effective_gain_db(settings, role, fs, loudness), driver.invert);
  }

  if (band == BAND_DELAY) {
    return biquad::allpass_delay(clampf(driver.delay_mm, 0.0f, DELAY_MAX_MM), fs);
  }

  if (band >= BAND_CROSSOVER_START && band < BAND_CROSSOVER_START + BAND_CROSSOVER_COUNT) {
    return crossover_section(settings.filter, is_tweeter, band - BAND_CROSSOVER_START, effective_frequency(settings), fs);
  }

  if (band >= BAND_PEQ_START && band < BAND_PEQ_START + NUMBER_PEQ_BANDS) {
    return peq_coefficients(driver.peq[band - BAND_PEQ_START], fs);
  }

  if (band == BAND_SUBSONIC && !is_tweeter) {
    if (settings.subsonic_frequency < SUBSONIC_FREQUENCY_MIN) return biquad::passthrough();
    return biquad::highpass(clampf(settings.subsonic_frequency, SUBSONIC_FREQUENCY_MIN, SUBSONIC_FREQUENCY_MAX),
                            biquad::Q_BUTTERWORTH, fs);
  }

  if (band == BAND_LINKWITZ && !is_tweeter) {
    if (settings.linkwitz_f0 < LINKWITZ_FREQUENCY_MIN || settings.linkwitz_fp < LINKWITZ_FREQUENCY_MIN) return biquad::passthrough();
    return biquad::linkwitz_transform(clampf(settings.linkwitz_f0, LINKWITZ_FREQUENCY_MIN, LINKWITZ_FREQUENCY_MAX),
                                      clampf(settings.linkwitz_q0, LINKWITZ_Q_MIN, LINKWITZ_Q_MAX),
                                      clampf(settings.linkwitz_fp, LINKWITZ_FREQUENCY_MIN, LINKWITZ_FREQUENCY_MAX),
                                      clampf(settings.linkwitz_qp, LINKWITZ_Q_MIN, LINKWITZ_Q_MAX), fs);
  }

  if (band == BAND_BAFFLE_STEP && !is_tweeter) {
    const BaffleStep step = baffle_step(settings.baffle_width_cm, settings.baffle_placement);
    return biquad::low_shelf(step.frequency, step.gain_db, biquad::Q_BUTTERWORTH, fs);
  }

  if (band == BAND_NOTCH && is_tweeter) {
    // only a cut, a boost at the breakup frequency would make it worse
    if (settings.notch_frequency < NOTCH_FREQUENCY_MIN || settings.notch_gain_db >= 0.0f) return biquad::passthrough();
    return biquad::peaking(clampf(settings.notch_frequency, NOTCH_FREQUENCY_MIN, NOTCH_FREQUENCY_MAX),
                           clampf(settings.notch_gain_db, NOTCH_GAIN_MIN_DB, 0.0f),
                           clampf(settings.notch_q, NOTCH_Q_MIN, NOTCH_Q_MAX), fs);
  }

  if (band == BAND_AIR && is_tweeter) {
    return biquad::high_shelf(AIR_FREQUENCY, clampf(settings.air_gain_db, -AIR_GAIN_MAX_DB, AIR_GAIN_MAX_DB),
                              biquad::Q_BUTTERWORTH, fs);
  }

  if (band == BAND_LOUDNESS_BASS && !is_tweeter) {
    return biquad::low_shelf(LOUDNESS_BASS_FREQUENCY, loudness.bass_db, biquad::Q_BUTTERWORTH, fs);
  }

  if (band == BAND_LOUDNESS_TREBLE && is_tweeter) {
    return biquad::high_shelf(LOUDNESS_TREBLE_FREQUENCY, loudness.treble_db, biquad::Q_BUTTERWORTH, fs);
  }

  return biquad::passthrough();
}

void encode_chain(const Settings &settings, DriverRole role, float fs, const LoudnessBoost &loudness,
                  uint8_t out[NUMBER_BANDS * biquad::ENCODED_SIZE]) {
  for (uint8_t band = 0; band < NUMBER_BANDS; band++) {
    biquad::encode(band_coefficients(settings, role, band, fs, loudness), out + band * biquad::ENCODED_SIZE);
  }
}

}  // namespace esphome::tas58xx::crossover
