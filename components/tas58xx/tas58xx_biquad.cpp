#include "tas58xx_biquad.h"

#include <cmath>

namespace esphome::tas58xx::biquad {

static constexpr double PI = 3.14159265358979323846;
static constexpr double SPEED_OF_SOUND_MM_PER_S = 343000.0;

// Q5.27 limits
static constexpr double Q5_27_SCALE = 134217728.0;  // 2^27
static constexpr double Q5_27_MAX = 2147483647.0;
static constexpr double Q5_27_MIN = -2147483648.0;

// gains smaller than this are written as passthrough
static constexpr double GAIN_EPSILON_DB = 0.01;

// keep filters inside the valid digital range
static bool valid_frequency(double fc, double fs) { return fs > 0.0 && fc > 0.0 && fc < fs / 2.0; }

static Coefficients normalise(double b0, double b1, double b2, double a0, double a1, double a2) {
  Coefficients c;
  c.b0 = b0 / a0;
  c.b1 = b1 / a0;
  c.b2 = b2 / a0;
  c.a1 = a1 / a0;
  c.a2 = a2 / a0;
  return c;
}

Coefficients passthrough() { return Coefficients{}; }

Coefficients gain(double gain_db, bool invert) {
  Coefficients c;
  c.b0 = std::pow(10.0, gain_db / 20.0);
  if (invert) c.b0 = -c.b0;
  return c;
}

Coefficients lowpass(double fc, double q, double fs) {
  if (!valid_frequency(fc, fs) || q <= 0.0) return passthrough();
  const double w0 = 2.0 * PI * fc / fs;
  const double cos_w0 = std::cos(w0);
  const double alpha = std::sin(w0) / (2.0 * q);
  return normalise((1.0 - cos_w0) / 2.0, 1.0 - cos_w0, (1.0 - cos_w0) / 2.0,
                   1.0 + alpha, -2.0 * cos_w0, 1.0 - alpha);
}

Coefficients highpass(double fc, double q, double fs) {
  if (!valid_frequency(fc, fs) || q <= 0.0) return passthrough();
  const double w0 = 2.0 * PI * fc / fs;
  const double cos_w0 = std::cos(w0);
  const double alpha = std::sin(w0) / (2.0 * q);
  return normalise((1.0 + cos_w0) / 2.0, -(1.0 + cos_w0), (1.0 + cos_w0) / 2.0,
                   1.0 + alpha, -2.0 * cos_w0, 1.0 - alpha);
}

Coefficients peaking(double fc, double gain_db, double q, double fs) {
  if (!valid_frequency(fc, fs) || q <= 0.0 || std::fabs(gain_db) < GAIN_EPSILON_DB) return passthrough();
  const double a = std::pow(10.0, gain_db / 40.0);
  const double w0 = 2.0 * PI * fc / fs;
  const double cos_w0 = std::cos(w0);
  const double alpha = std::sin(w0) / (2.0 * q);
  return normalise(1.0 + alpha * a, -2.0 * cos_w0, 1.0 - alpha * a,
                   1.0 + alpha / a, -2.0 * cos_w0, 1.0 - alpha / a);
}

Coefficients low_shelf(double fc, double gain_db, double q, double fs) {
  if (!valid_frequency(fc, fs) || q <= 0.0 || std::fabs(gain_db) < GAIN_EPSILON_DB) return passthrough();
  const double a = std::pow(10.0, gain_db / 40.0);
  const double w0 = 2.0 * PI * fc / fs;
  const double cos_w0 = std::cos(w0);
  const double two_sqrt_a_alpha = 2.0 * std::sqrt(a) * std::sin(w0) / (2.0 * q);
  return normalise(a * ((a + 1.0) - (a - 1.0) * cos_w0 + two_sqrt_a_alpha),
                   2.0 * a * ((a - 1.0) - (a + 1.0) * cos_w0),
                   a * ((a + 1.0) - (a - 1.0) * cos_w0 - two_sqrt_a_alpha),
                   (a + 1.0) + (a - 1.0) * cos_w0 + two_sqrt_a_alpha,
                   -2.0 * ((a - 1.0) + (a + 1.0) * cos_w0),
                   (a + 1.0) + (a - 1.0) * cos_w0 - two_sqrt_a_alpha);
}

Coefficients high_shelf(double fc, double gain_db, double q, double fs) {
  if (!valid_frequency(fc, fs) || q <= 0.0 || std::fabs(gain_db) < GAIN_EPSILON_DB) return passthrough();
  const double a = std::pow(10.0, gain_db / 40.0);
  const double w0 = 2.0 * PI * fc / fs;
  const double cos_w0 = std::cos(w0);
  const double two_sqrt_a_alpha = 2.0 * std::sqrt(a) * std::sin(w0) / (2.0 * q);
  return normalise(a * ((a + 1.0) + (a - 1.0) * cos_w0 + two_sqrt_a_alpha),
                   -2.0 * a * ((a - 1.0) + (a + 1.0) * cos_w0),
                   a * ((a + 1.0) + (a - 1.0) * cos_w0 - two_sqrt_a_alpha),
                   (a + 1.0) - (a - 1.0) * cos_w0 + two_sqrt_a_alpha,
                   2.0 * ((a - 1.0) - (a + 1.0) * cos_w0),
                   (a + 1.0) - (a - 1.0) * cos_w0 - two_sqrt_a_alpha);
}

Coefficients linkwitz_transform(double f0, double q0, double fp, double qp, double fs) {
  if (!valid_frequency(f0, fs) || !valid_frequency(fp, fs) || q0 <= 0.0 || qp <= 0.0) return passthrough();

  // H(s) = (s^2 + (w0/q0)s + w0^2) / (s^2 + (wp/qp)s + wp^2), bilinear transform
  // pre-warped at the mean of the two frequencies (Linkwitz, linkwitzlab.com)
  const double w0 = 2.0 * PI * f0;
  const double wp = 2.0 * PI * fp;
  const double fc = (f0 + fp) / 2.0;
  const double k = 2.0 * PI * fc / std::tan(PI * fc / fs);
  const double k2 = k * k;

  return normalise(w0 * w0 + k * w0 / q0 + k2, 2.0 * (w0 * w0 - k2), w0 * w0 - k * w0 / q0 + k2,
                   wp * wp + k * wp / qp + k2, 2.0 * (wp * wp - k2), wp * wp - k * wp / qp + k2);
}

Coefficients allpass_delay(double delay_mm, double fs) {
  if (delay_mm <= 0.0 || fs <= 0.0) return passthrough();

  // analog 1st order all-pass (w - s)/(w + s) has group delay 2/w at DC, so w = 2/T
  const double delay_s = delay_mm / SPEED_OF_SOUND_MM_PER_S;
  double fc = 1.0 / (PI * delay_s);
  if (fc >= fs / 2.0) fc = fs / 2.0 - 1.0;

  // bilinear transform gives H(z) = (c + z^-1) / (1 + c*z^-1) with c = (t - 1)/(t + 1)
  // group delay at DC is (1 - c)/(1 + c) = 1/t samples ~ T*fs
  // note: the snapclient port used c = (1 - t)/(1 + t), which gives t samples instead
  const double t = std::tan(PI * fc / fs);
  const double c = (t - 1.0) / (t + 1.0);

  Coefficients coeffs;
  coeffs.b0 = c;
  coeffs.b1 = 1.0;
  coeffs.b2 = 0.0;
  coeffs.a1 = c;
  coeffs.a2 = 0.0;
  return coeffs;
}

int32_t to_q5_27(double value) {
  double scaled = std::round(value * Q5_27_SCALE);
  if (scaled > Q5_27_MAX) scaled = Q5_27_MAX;
  if (scaled < Q5_27_MIN) scaled = Q5_27_MIN;
  return static_cast<int32_t>(scaled);
}

void encode(const Coefficients &c, uint8_t out[ENCODED_SIZE]) {
  const double values[5] = {c.b0, c.b1, c.b2, -c.a1, -c.a2};
  for (uint8_t i = 0; i < 5; i++) {
    const uint32_t raw = static_cast<uint32_t>(to_q5_27(values[i]));
    out[i * 4 + 0] = static_cast<uint8_t>(raw >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(raw >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(raw >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(raw);
  }
}

}  // namespace esphome::tas58xx::biquad
