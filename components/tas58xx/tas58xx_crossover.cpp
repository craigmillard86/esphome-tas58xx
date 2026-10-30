#include "tas58xx_crossover.h"

#include <cmath>

namespace esphome::tas58xx::crossover {

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

biquad::Coefficients band_coefficients(const Settings &settings, DriverRole role, uint8_t band, float fs,
                                       const LoudnessBoost &loudness) {
  const DriverSettings &driver = settings.driver[role];
  const bool is_tweeter = (role == ROLE_TWEETER);

  if (band == BAND_GAIN) {
    return biquad::gain(clampf(driver.gain_db, DRIVER_GAIN_MIN_DB, DRIVER_GAIN_MAX_DB), driver.invert);
  }

  if (band == BAND_DELAY) {
    return biquad::allpass_delay(clampf(driver.delay_mm, 0.0f, DELAY_MAX_MM), fs);
  }

  if (band >= BAND_CROSSOVER_START && band < BAND_CROSSOVER_START + BAND_CROSSOVER_COUNT) {
    return crossover_section(settings.filter, is_tweeter, band - BAND_CROSSOVER_START, settings.frequency, fs);
  }

  if (band >= BAND_PEQ_START && band < BAND_PEQ_START + NUMBER_PEQ_BANDS) {
    return peq_coefficients(driver.peq[band - BAND_PEQ_START], fs);
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
