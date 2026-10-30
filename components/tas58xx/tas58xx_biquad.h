#pragma once

// Biquad coefficient maths for the TAS58xx DSP.
// Ported from the snapclient TAS5805M bi-amp work (tas5805m_biamp.c).
// Plain C++ with no ESPHome dependencies so it can be unit tested on a host.

#include <cstdint>

namespace esphome::tas58xx::biquad {

// coefficients in standard form with a0 normalised to 1
// H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
struct Coefficients {
  double b0{1.0};
  double b1{0.0};
  double b2{0.0};
  double a1{0.0};
  double a2{0.0};
};

static constexpr uint8_t ENCODED_SIZE = 20;  // 5 coefficients x 4 bytes

// Q factors of the 2nd order sections
static constexpr double Q_BUTTERWORTH = 0.70710678118654752;  // 1/sqrt(2)
static constexpr double Q_LR2 = 0.5;                          // two cascaded 1st order Butterworth
static constexpr double Q_BW4_STAGE1 = 0.54119610014619698;   // 4th order Butterworth sections
static constexpr double Q_BW4_STAGE2 = 1.30656296487637652;

Coefficients passthrough();
Coefficients gain(double gain_db, bool invert);

// RBJ cookbook 2nd order filters, bilinear transform with pre-warping
Coefficients lowpass(double fc, double q, double fs);
Coefficients highpass(double fc, double q, double fs);
Coefficients peaking(double fc, double gain_db, double q, double fs);
Coefficients low_shelf(double fc, double gain_db, double q, double fs);
Coefficients high_shelf(double fc, double gain_db, double q, double fs);

// 1st order all-pass whose group delay at low frequencies is delay_mm / speed of sound.
// Only an approximation of a true delay: group delay falls off above ~1/(pi*T).
Coefficients allpass_delay(double delay_mm, double fs);

// convert to the TAS58xx register layout: B0 B1 B2 A1 A2 as big endian Q5.27
// the DSP adds the feedback terms so A1 and A2 are negated
void encode(const Coefficients &c, uint8_t out[ENCODED_SIZE]);

int32_t to_q5_27(double value);

}  // namespace esphome::tas58xx::biquad
