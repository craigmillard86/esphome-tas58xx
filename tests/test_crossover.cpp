// Host unit tests for the TAS58xx biquad and crossover coefficient generator.
// Build and run with tests/run_tests.sh (needs any C++17 host compiler).

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../components/tas58xx/tas58xx_biquad.h"
#include "../components/tas58xx/tas58xx_crossover.h"

using namespace esphome::tas58xx;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                          \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__);                   \
      std::printf("\n");                          \
    }                                             \
  } while (0)

static constexpr double FS = 48000.0;
static constexpr double PI = 3.14159265358979323846;

static std::complex<double> response(const biquad::Coefficients &c, double f, double fs = FS) {
  const std::complex<double> z1 = std::polar(1.0, -2.0 * PI * f / fs);
  const std::complex<double> z2 = z1 * z1;
  return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

static double db(std::complex<double> h) { return 20.0 * std::log10(std::abs(h)); }

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static uint32_t word(const uint8_t *bytes, int index) {
  const uint8_t *p = bytes + index * 4;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static bool words_near(const uint8_t *a, const uint8_t *b, int max_lsb) {
  for (int i = 0; i < 5; i++) {
    const int64_t diff = int64_t(int32_t(word(a, i))) - int64_t(int32_t(word(b, i)));
    if (diff > max_lsb || diff < -max_lsb) return false;
  }
  return true;
}

// reference values from scipy.signal.butter(2, 2500, fs=48000) - one LR4 section
static void test_lr4_2500hz_reference() {
  const biquad::Coefficients lp = crossover::crossover_section(crossover::FILTER_LR4, false, 0, 2500.0f, FS);
  const biquad::Coefficients hp = crossover::crossover_section(crossover::FILTER_LR4, true, 0, 2500.0f, FS);

  CHECK(near(lp.b0, 0.021620718376, 1e-9), "LR4 LP b0 %.12f", lp.b0);
  CHECK(near(lp.b1, 0.043241436753, 1e-9), "LR4 LP b1 %.12f", lp.b1);
  CHECK(near(lp.b2, 0.021620718376, 1e-9), "LR4 LP b2 %.12f", lp.b2);
  CHECK(near(lp.a1, -1.543121131237, 1e-9), "LR4 LP a1 %.12f", lp.a1);
  CHECK(near(lp.a2, 0.629604004743, 1e-9), "LR4 LP a2 %.12f", lp.a2);

  CHECK(near(hp.b0, 0.793181283995, 1e-9), "LR4 HP b0 %.12f", hp.b0);
  CHECK(near(hp.b1, -1.586362567990, 1e-9), "LR4 HP b1 %.12f", hp.b1);
  CHECK(near(hp.b2, 0.793181283995, 1e-9), "LR4 HP b2 %.12f", hp.b2);
  CHECK(near(hp.a1, -1.543121131237, 1e-9), "LR4 HP a1 %.12f", hp.a1);
  CHECK(near(hp.a2, 0.629604004743, 1e-9), "LR4 HP a2 %.12f", hp.a2);

  // register image: B0 B1 B2 -A1 -A2, big endian Q5.27
  const uint8_t lp_expected[20] = {0x00, 0x2C, 0x47, 0x7C, 0x00, 0x58, 0x8E, 0xF7, 0x00, 0x2C,
                                   0x47, 0x7C, 0x0C, 0x58, 0x4F, 0xE4, 0xFA, 0xF6, 0x92, 0x2D};
  const uint8_t hp_expected[20] = {0x06, 0x58, 0x6F, 0x6E, 0xF3, 0x4F, 0x21, 0x24, 0x06, 0x58,
                                   0x6F, 0x6E, 0x0C, 0x58, 0x4F, 0xE4, 0xFA, 0xF6, 0x92, 0x2D};
  uint8_t encoded[20];
  biquad::encode(lp, encoded);
  CHECK(words_near(encoded, lp_expected, 1), "LR4 LP register image");
  biquad::encode(hp, encoded);
  CHECK(words_near(encoded, hp_expected, 1), "LR4 HP register image");

  // second LR4 section is identical, remaining crossover bands are passthrough
  const biquad::Coefficients hp2 = crossover::crossover_section(crossover::FILTER_LR4, true, 1, 2500.0f, FS);
  CHECK(hp2.b0 == hp.b0 && hp2.a1 == hp.a1 && hp2.a2 == hp.a2, "LR4 second section matches first");
  const biquad::Coefficients hp3 = crossover::crossover_section(crossover::FILTER_LR4, true, 2, 2500.0f, FS);
  CHECK(hp3.b0 == 1.0 && hp3.b1 == 0.0 && hp3.a1 == 0.0, "LR4 third section is passthrough");
}

static std::complex<double> chain_response(crossover::CrossoverFilter filter, bool highpass, double f) {
  std::complex<double> h = 1.0;
  for (uint8_t i = 0; i < crossover::crossover_sections(filter); i++) {
    h *= response(crossover::crossover_section(filter, highpass, i, 2500.0f, FS), f);
  }
  return h;
}

static void test_crossover_responses() {
  // LR4: -6dB at crossover on both, in-phase sum is all-pass (flat)
  CHECK(near(db(chain_response(crossover::FILTER_LR4, false, 2500.0)), -6.0206, 0.01), "LR4 LP -6dB at fc");
  CHECK(near(db(chain_response(crossover::FILTER_LR4, true, 2500.0)), -6.0206, 0.01), "LR4 HP -6dB at fc");
  for (double f = 20.0; f < 20000.0; f *= 1.25) {
    const double sum = db(chain_response(crossover::FILTER_LR4, false, f) + chain_response(crossover::FILTER_LR4, true, f));
    CHECK(near(sum, 0.0, 0.01), "LR4 sum flat at %.0fHz: %.4fdB", f, sum);
  }
  // 24dB/oct: two octaves away the tweeter is about 48dB down
  CHECK(db(chain_response(crossover::FILTER_LR4, true, 625.0)) < -45.0, "LR4 HP slope");

  // LR2: -6dB at fc, flat sum when one driver is inverted
  CHECK(near(db(chain_response(crossover::FILTER_LR2, false, 2500.0)), -6.0206, 0.01), "LR2 LP -6dB at fc");
  for (double f = 20.0; f < 20000.0; f *= 1.25) {
    const double sum = db(chain_response(crossover::FILTER_LR2, false, f) - chain_response(crossover::FILTER_LR2, true, f));
    CHECK(near(sum, 0.0, 0.01), "LR2 inverted sum flat at %.0fHz: %.4fdB", f, sum);
  }

  // Butterworth: -3dB at fc
  CHECK(near(db(chain_response(crossover::FILTER_BW2, false, 2500.0)), -3.0103, 0.01), "BW2 LP -3dB at fc");
  CHECK(near(db(chain_response(crossover::FILTER_BW2, true, 2500.0)), -3.0103, 0.01), "BW2 HP -3dB at fc");
  CHECK(near(db(chain_response(crossover::FILTER_BW4, false, 2500.0)), -3.0103, 0.01), "BW4 LP -3dB at fc");
  CHECK(near(db(chain_response(crossover::FILTER_BW4, true, 2500.0)), -3.0103, 0.01), "BW4 HP -3dB at fc");

  // BW4 poles match scipy.signal.butter(4, 2500, fs=48000, output='sos')
  const biquad::Coefficients s1 = crossover::crossover_section(crossover::FILTER_BW4, true, 0, 2500.0f, FS);
  const biquad::Coefficients s2 = crossover::crossover_section(crossover::FILTER_BW4, true, 1, 2500.0f, FS);
  CHECK(near(s1.a1, -1.460217505427, 1e-9) && near(s1.a2, 0.542054117768, 1e-9), "BW4 section 1 poles");
  CHECK(near(s2.a1, -1.686415084984, 1e-9) && near(s2.a2, 0.780928742740, 1e-9), "BW4 section 2 poles");

  // passband is unity
  CHECK(near(db(chain_response(crossover::FILTER_LR4, false, 50.0)), 0.0, 0.01), "LR4 LP passband");
  CHECK(near(db(chain_response(crossover::FILTER_LR4, true, 15000.0)), 0.0, 0.05), "LR4 HP passband");
}

static void test_eq_filters() {
  CHECK(near(db(response(biquad::peaking(1000.0, 6.0, 1.4, FS), 1000.0)), 6.0, 0.01), "peaking +6dB at fc");
  CHECK(near(db(response(biquad::peaking(1000.0, -9.0, 4.0, FS), 1000.0)), -9.0, 0.01), "peaking -9dB at fc");
  CHECK(near(db(response(biquad::peaking(1000.0, 6.0, 1.4, FS), 50.0)), 0.0, 0.1), "peaking flat far away");
  CHECK(near(db(response(biquad::low_shelf(200.0, 6.0, 0.7071, FS), 10.0)), 6.0, 0.05), "low shelf DC gain");
  CHECK(near(db(response(biquad::low_shelf(200.0, 6.0, 0.7071, FS), 10000.0)), 0.0, 0.05), "low shelf HF gain");
  CHECK(near(db(response(biquad::high_shelf(4000.0, 4.0, 0.7071, FS), 20000.0)), 4.0, 0.1), "high shelf HF gain");
  CHECK(near(db(response(biquad::high_shelf(4000.0, 4.0, 0.7071, FS), 50.0)), 0.0, 0.05), "high shelf LF gain");

  const biquad::Coefficients flat = biquad::peaking(1000.0, 0.0, 1.0, FS);
  CHECK(flat.b0 == 1.0 && flat.b1 == 0.0 && flat.a1 == 0.0, "0dB peaking is passthrough");

  const biquad::Coefficients g = biquad::gain(-3.0, false);
  CHECK(near(db(response(g, 1000.0)), -3.0, 1e-9), "gain -3dB");
  const biquad::Coefficients inv = biquad::gain(0.0, true);
  CHECK(inv.b0 == -1.0, "polarity invert");
}

// same section at the 96kHz DSP processing rate the component configures (register 0x46 = 0x01)
// reference values from scipy.signal.butter(2, 2500, fs=96000)
static void test_lr4_2500hz_reference_96k() {
  const double fs = 96000.0;
  const biquad::Coefficients lp = crossover::crossover_section(crossover::FILTER_LR4, false, 0, 2500.0f, fs);
  const biquad::Coefficients hp = crossover::crossover_section(crossover::FILTER_LR4, true, 0, 2500.0f, fs);
  CHECK(near(lp.b0, 0.005988546285, 1e-9) && near(lp.a1, -1.769471038228, 1e-9) && near(lp.a2, 0.793425223367, 1e-9),
        "LR4 LP 96k coefficients");
  CHECK(near(hp.b0, 0.890724065399, 1e-9) && near(hp.b1, -1.781448130798, 1e-9), "LR4 HP 96k coefficients");

  const uint8_t lp_expected[20] = {0x00, 0x0C, 0x43, 0xB9, 0x00, 0x18, 0x87, 0x72, 0x00, 0x0C,
                                   0x43, 0xB9, 0x0E, 0x27, 0xE0, 0x6F, 0xF9, 0xA7, 0x10, 0xAD};
  const uint8_t hp_expected[20] = {0x07, 0x20, 0x33, 0xF0, 0xF1, 0xBF, 0x98, 0x1F, 0x07, 0x20,
                                   0x33, 0xF0, 0x0E, 0x27, 0xE0, 0x6F, 0xF9, 0xA7, 0x10, 0xAD};
  uint8_t encoded[20];
  biquad::encode(lp, encoded);
  CHECK(words_near(encoded, lp_expected, 1), "LR4 LP 96k register image");
  biquad::encode(hp, encoded);
  CHECK(words_near(encoded, hp_expected, 1), "LR4 HP 96k register image");
}

static biquad::Coefficients decode(const uint8_t bytes[20]) {
  double v[5];
  for (int i = 0; i < 5; i++) v[i] = double(int32_t(word(bytes, i))) / 134217728.0;
  biquad::Coefficients c;
  c.b0 = v[0];
  c.b1 = v[1];
  c.b2 = v[2];
  c.a1 = -v[3];
  c.a2 = -v[4];
  return c;
}

// mrtoy-me's PPC3 generated table: BQ1 equaliser 20Hz -15dB Q2 (tas58xx_eq_bands.h EQ_BAND_COEFFICIENTS_MF)
// Decoding it with our register convention must give the documented filter. This checks the byte order,
// the Q5.27 scaling and the a1/a2 sign convention against TI's own tool, and shows the DSP rate is 96kHz.
static void test_register_format_matches_ppc3() {
  const uint8_t ppc3[20] = {0x07, 0xfc, 0xe8, 0x34, 0xf0, 0x07, 0x87, 0x01, 0x07, 0xfb,
                            0x91, 0xb0, 0x0f, 0xf8, 0x78, 0xff, 0xf8, 0x07, 0x86, 0x1b};
  const biquad::Coefficients c = decode(ppc3);
  CHECK(near(db(response(c, 20.0, 96000.0)), -15.0, 0.05), "PPC3 row is -15dB at 20Hz when fs=96kHz: %.2f",
        db(response(c, 20.0, 96000.0)));
  CHECK(near(db(response(c, 1000.0, 96000.0)), 0.0, 0.05), "PPC3 row is flat at 1kHz");
  CHECK(!near(db(response(c, 20.0, 48000.0)), -15.0, 1.0), "PPC3 row is not centred on 20Hz at 48kHz");

  // round trip through our encoder is lossless
  uint8_t encoded[20];
  biquad::encode(c, encoded);
  CHECK(std::memcmp(encoded, ppc3, 20) == 0, "encode(decode(x)) == x");
}

static void test_allpass_delay() {
  const double delay_mm = 20.0;
  const double expected_s = delay_mm / 343000.0;
  const biquad::Coefficients ap = biquad::allpass_delay(delay_mm, FS);
  for (double f = 20.0; f < 20000.0; f *= 2.0) {
    CHECK(near(db(response(ap, f)), 0.0, 1e-9), "all-pass is flat at %.0fHz", f);
  }
  // group delay from phase slope at low frequency
  const double f = 100.0, df = 1.0;
  const double p1 = std::arg(response(ap, f - df)), p2 = std::arg(response(ap, f + df));
  const double group_delay = -(p2 - p1) / (2.0 * PI * 2.0 * df);
  CHECK(near(group_delay, expected_s, expected_s * 0.05), "all-pass group delay %.2fus expected %.2fus",
        group_delay * 1e6, expected_s * 1e6);

  const biquad::Coefficients none = biquad::allpass_delay(0.0, FS);
  CHECK(none.b0 == 1.0 && none.b1 == 0.0, "zero delay is passthrough");
}

static void test_encoding() {
  uint8_t out[20];
  biquad::encode(biquad::passthrough(), out);
  const uint8_t unity[20] = {0x08, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(std::memcmp(out, unity, 20) == 0, "passthrough encodes as 0x08000000");

  biquad::Coefficients c;
  c.b0 = 0.0;
  c.a1 = 1.0;   // stored negated
  c.a2 = -0.5;  // stored negated
  biquad::encode(c, out);
  CHECK(word(out, 3) == 0xF8000000u, "a1 negated: %08X", word(out, 3));
  CHECK(word(out, 4) == 0x04000000u, "a2 negated: %08X", word(out, 4));

  CHECK(biquad::to_q5_27(100.0) == INT32_MAX, "Q5.27 clamps high");
  CHECK(biquad::to_q5_27(-100.0) == INT32_MIN, "Q5.27 clamps low");
}

static void test_chain() {
  crossover::Settings s;
  s.filter = crossover::FILTER_LR4;
  s.frequency = 2500.0f;
  s.driver[crossover::ROLE_TWEETER].gain_db = -3.0f;
  s.driver[crossover::ROLE_MID].peq[0] = {300.0f, -4.0f, 2.0f, crossover::PEQ_PEAKING};
  const crossover::LoudnessBoost no_boost;

  uint8_t tweeter[crossover::NUMBER_BANDS * 20];
  uint8_t mid[crossover::NUMBER_BANDS * 20];
  crossover::encode_chain(s, crossover::ROLE_TWEETER, FS, no_boost, tweeter);
  crossover::encode_chain(s, crossover::ROLE_MID, FS, no_boost, mid);

  uint8_t expected[20];
  biquad::encode(biquad::highpass(2500.0, biquad::Q_BUTTERWORTH, FS), expected);
  CHECK(std::memcmp(tweeter + 2 * 20, expected, 20) == 0, "tweeter band 2 is high-pass");
  CHECK(std::memcmp(tweeter + 3 * 20, expected, 20) == 0, "tweeter band 3 is high-pass");
  biquad::encode(biquad::lowpass(2500.0, biquad::Q_BUTTERWORTH, FS), expected);
  CHECK(std::memcmp(mid + 2 * 20, expected, 20) == 0, "mid band 2 is low-pass");

  biquad::encode(biquad::passthrough(), expected);
  CHECK(std::memcmp(tweeter + 4 * 20, expected, 20) == 0, "tweeter band 4 passthrough");
  CHECK(std::memcmp(tweeter + 14 * 20, expected, 20) == 0, "tweeter band 14 passthrough without loudness");

  biquad::encode(biquad::gain(-3.0, false), expected);
  CHECK(std::memcmp(tweeter, expected, 20) == 0, "tweeter band 0 is -3dB");

  biquad::encode(biquad::peaking(300.0, -4.0, 2.0, FS), expected);
  CHECK(std::memcmp(mid + 6 * 20, expected, 20) == 0, "mid PEQ 1 in band 6");

  // every tweeter band combined is still a high-pass: deep cut at 100Hz
  std::complex<double> h = 1.0;
  for (uint8_t band = 0; band < crossover::NUMBER_BANDS; band++) {
    h *= response(crossover::band_coefficients(s, crossover::ROLE_TWEETER, band, FS, no_boost), 100.0);
  }
  CHECK(db(h) < -100.0, "tweeter chain blocks 100Hz: %.1fdB", db(h));

  // frequency is clamped to the supported range
  const biquad::Coefficients low = crossover::crossover_section(crossover::FILTER_LR4, true, 0, 50.0f, FS);
  CHECK(near(db(response(low, 500.0)), -3.0103, 0.01), "crossover frequency clamps to 500Hz");
}

static void test_loudness() {
  crossover::Settings s;
  s.loudness = true;
  s.loudness_reference_db = 0.0f;

  crossover::LoudnessBoost b = crossover::loudness_boost(s, -15.0f);
  CHECK(b.bass_db == 3.0f && b.treble_db == 2.0f, "loudness at -15dB: %.1f %.1f", b.bass_db, b.treble_db);
  b = crossover::loudness_boost(s, -60.0f);
  CHECK(b.bass_db == 6.0f && b.treble_db == 4.0f, "loudness capped");
  b = crossover::loudness_boost(s, 3.0f);
  CHECK(b.bass_db == 0.0f && b.treble_db == 0.0f, "no loudness above reference");

  s.loudness = false;
  b = crossover::loudness_boost(s, -40.0f);
  CHECK(b.bass_db == 0.0f && b.treble_db == 0.0f, "loudness off");

  s.loudness = true;
  b = crossover::loudness_boost(s, -30.0f);
  const biquad::Coefficients bass = crossover::band_coefficients(s, crossover::ROLE_MID, 13, FS, b);
  CHECK(near(db(response(bass, 20.0)), 6.0, 0.1), "loudness bass shelf on mid band 13");
  const biquad::Coefficients treble = crossover::band_coefficients(s, crossover::ROLE_TWEETER, 14, FS, b);
  CHECK(near(db(response(treble, 20000.0)), 4.0, 0.2), "loudness treble shelf on tweeter band 14");
}

static void test_speaker_compensation() {
  const double fs = 96000.0;
  const crossover::LoudnessBoost no_boost;
  crossover::Settings s;

  // everything off is passthrough on both drivers
  for (uint8_t band : {crossover::BAND_SUBSONIC, crossover::BAND_BAFFLE_STEP, crossover::BAND_AIR}) {
    for (uint8_t role = 0; role < crossover::NUMBER_ROLES; role++) {
      const biquad::Coefficients c = crossover::band_coefficients(s, static_cast<crossover::DriverRole>(role), band, fs, no_boost);
      CHECK(c.b0 == 1.0 && c.b1 == 0.0 && c.a1 == 0.0, "band %d role %d passthrough when off", band, role);
    }
  }

  // subsonic: Butterworth high-pass on the mid only
  s.subsonic_frequency = 30.0f;
  const biquad::Coefficients sub = crossover::band_coefficients(s, crossover::ROLE_MID, crossover::BAND_SUBSONIC, fs, no_boost);
  CHECK(near(db(response(sub, 30.0, fs)), -3.0103, 0.01), "subsonic -3dB at 30Hz");
  CHECK(near(db(response(sub, 1000.0, fs)), 0.0, 0.01), "subsonic flat at 1kHz");
  const biquad::Coefficients sub_tw = crossover::band_coefficients(s, crossover::ROLE_TWEETER, crossover::BAND_SUBSONIC, fs, no_boost);
  CHECK(sub_tw.b0 == 1.0 && sub_tw.a1 == 0.0, "subsonic not on tweeter");
  s.subsonic_frequency = 5.0f;
  const biquad::Coefficients sub_off = crossover::band_coefficients(s, crossover::ROLE_MID, crossover::BAND_SUBSONIC, fs, no_boost);
  CHECK(sub_off.b0 == 1.0 && sub_off.a1 == 0.0, "subsonic below 10Hz is off");

  // baffle step: 20cm freestanding is +6dB below ~546Hz, near wall +3dB, corner off
  const crossover::BaffleStep step = crossover::baffle_step(20.0f, crossover::PLACEMENT_FREESTANDING);
  CHECK(near(step.frequency, 545.9, 0.5) && step.gain_db == 6.0f, "baffle step 20cm: %.1fHz %.1fdB", step.frequency, step.gain_db);
  CHECK(crossover::baffle_step(20.0f, crossover::PLACEMENT_NEAR_WALL).gain_db == 3.0f, "baffle step near wall");
  CHECK(crossover::baffle_step(20.0f, crossover::PLACEMENT_CORNER).gain_db == 0.0f, "baffle step corner");
  s.baffle_width_cm = 20.0f;
  const biquad::Coefficients baffle = crossover::band_coefficients(s, crossover::ROLE_MID, crossover::BAND_BAFFLE_STEP, fs, no_boost);
  CHECK(near(db(response(baffle, 20.0, fs)), 6.0, 0.1), "baffle step +6dB at 20Hz");
  CHECK(near(db(response(baffle, 10000.0, fs)), 0.0, 0.05), "baffle step flat at 10kHz");

  // notch: cut only, on the tweeter only
  s.notch_frequency = 18000.0f;
  s.notch_gain_db = -10.0f;
  s.notch_q = 5.0f;
  const biquad::Coefficients notch = crossover::band_coefficients(s, crossover::ROLE_TWEETER, crossover::BAND_NOTCH, fs, no_boost);
  CHECK(near(db(response(notch, 18000.0, fs)), -10.0, 0.01), "notch -10dB at 18kHz");
  CHECK(near(db(response(notch, 5000.0, fs)), 0.0, 0.1), "notch narrow");
  const biquad::Coefficients notch_mid = crossover::band_coefficients(s, crossover::ROLE_MID, crossover::BAND_NOTCH, fs, no_boost);
  CHECK(!near(db(response(notch_mid, 18000.0, fs)), -10.0, 1.0), "notch not on mid (band 12 is baffle step there)");
  s.notch_gain_db = 3.0f;
  const biquad::Coefficients notch_boost = crossover::band_coefficients(s, crossover::ROLE_TWEETER, crossover::BAND_NOTCH, fs, no_boost);
  CHECK(notch_boost.b0 == 1.0 && notch_boost.a1 == 0.0, "notch ignores a boost");

  // air shelf at 10kHz on the tweeter
  s.air_gain_db = 3.0f;
  const biquad::Coefficients air = crossover::band_coefficients(s, crossover::ROLE_TWEETER, crossover::BAND_AIR, fs, no_boost);
  CHECK(near(db(response(air, 40000.0, fs)), 3.0, 0.2), "air shelf +3dB high up: %.2f", db(response(air, 40000.0, fs)));
  CHECK(near(db(response(air, 1000.0, fs)), 0.0, 0.1), "air shelf flat at 1kHz");

  // the tweeter is still a deep high-pass with all of these on
  s.filter = crossover::FILTER_LR4;
  std::complex<double> h = 1.0;
  for (uint8_t band = 0; band < crossover::NUMBER_BANDS; band++) {
    h *= response(crossover::band_coefficients(s, crossover::ROLE_TWEETER, band, fs, no_boost), 100.0, fs);
  }
  CHECK(db(h) < -100.0, "tweeter chain still blocks 100Hz: %.1fdB", db(h));
}

static void test_linkwitz_and_headroom() {
  const double fs = 96000.0;
  const crossover::LoudnessBoost no_boost;

  // Linkwitz transform f0 80Hz Q0 0.9 -> fp 50Hz Qp 0.6: LF gain (80/50)^2 = +8.16dB, HF unchanged
  const biquad::Coefficients lt = biquad::linkwitz_transform(80.0, 0.9, 50.0, 0.6, fs);
  CHECK(near(db(response(lt, 2.0, fs)), 8.1648, 0.05), "Linkwitz LF gain %.2fdB", db(response(lt, 2.0, fs)));
  CHECK(near(db(response(lt, 5000.0, fs)), 0.0, 0.05), "Linkwitz HF unity");
  // sealed box (2nd order HP at f0/Q0) times LT equals the target alignment (2nd order HP at fp/Qp)
  for (double f : {30.0, 50.0, 80.0, 150.0}) {
    const double box = db(response(biquad::highpass(80.0, 0.9, fs), f, fs) * response(lt, f, fs));
    const double target = db(response(biquad::highpass(50.0, 0.6, fs), f, fs));
    CHECK(near(box, target, 0.15), "Linkwitz gives target alignment at %.0fHz: %.2f vs %.2f", f, box, target);
  }

  crossover::Settings s;
  const biquad::Coefficients off = crossover::band_coefficients(s, crossover::ROLE_MID, crossover::BAND_LINKWITZ, fs, no_boost);
  CHECK(off.b0 == 1.0 && off.a1 == 0.0, "Linkwitz off by default");
  s.linkwitz_f0 = 80.0f; s.linkwitz_q0 = 0.9f; s.linkwitz_fp = 50.0f; s.linkwitz_qp = 0.6f;
  const biquad::Coefficients on = crossover::band_coefficients(s, crossover::ROLE_MID, crossover::BAND_LINKWITZ, fs, no_boost);
  CHECK(near(db(response(on, 2.0, fs)), 8.1648, 0.05), "Linkwitz on mid band 5");
  const biquad::Coefficients tw = crossover::band_coefficients(s, crossover::ROLE_TWEETER, crossover::BAND_LINKWITZ, fs, no_boost);
  CHECK(tw.b0 == 1.0 && tw.a1 == 0.0, "Linkwitz not on tweeter");

  // minimum crossover frequency from YAML protects the tweeter
  crossover::Settings m;
  m.frequency_min = 2000.0f;
  m.frequency = 800.0f;
  CHECK(crossover::effective_frequency(m) == 2000.0f, "crossover clamped to YAML minimum");
  const biquad::Coefficients hp = crossover::band_coefficients(m, crossover::ROLE_TWEETER, crossover::BAND_CROSSOVER_START, fs, no_boost);
  CHECK(near(db(response(hp, 2000.0, fs)), -3.0103, 0.01), "tweeter high-pass sits at the minimum");
  m.frequency = 3000.0f;
  CHECK(crossover::effective_frequency(m) == 3000.0f, "frequency above minimum unchanged");

  // peak gain: flat chain is 0dB, +6dB baffle step on the mid shows up, gain trim included on request
  crossover::Settings p;
  CHECK(near(crossover::chain_peak_db(p, crossover::ROLE_MID, fs, no_boost, true), 0.0, 0.05), "flat mid peak 0dB");
  p.baffle_width_cm = 20.0f;
  p.driver[crossover::ROLE_MID].gain_db = -2.0f;
  const float filters = crossover::chain_peak_db(p, crossover::ROLE_MID, fs, no_boost, false);
  const float total = crossover::chain_peak_db(p, crossover::ROLE_MID, fs, no_boost, true);
  CHECK(near(filters, 6.0, 0.1), "mid filter peak with baffle step %.2fdB", filters);
  CHECK(near(total, 4.0, 0.1), "mid total peak with -2dB trim %.2fdB", total);

  // headroom compensation lowers the trim by the filter boost so the chain peaks at the user trim
  CHECK(crossover::effective_gain_db(p, crossover::ROLE_MID, fs, no_boost) == -2.0f, "no compensation by default");
  p.headroom_compensation = true;
  const float comp = crossover::effective_gain_db(p, crossover::ROLE_MID, fs, no_boost);
  CHECK(near(comp, -8.0, 0.1), "compensated mid trim %.2fdB", comp);
  CHECK(near(crossover::chain_peak_db(p, crossover::ROLE_MID, fs, no_boost, true), -2.0, 0.1), "compensated mid peaks at trim");
  // a cut never raises the gain
  crossover::Settings c;
  c.headroom_compensation = true;
  c.driver[crossover::ROLE_TWEETER].peq[0] = {5000.0f, -6.0f, 1.0f, crossover::PEQ_PEAKING};
  CHECK(crossover::effective_gain_db(c, crossover::ROLE_TWEETER, fs, no_boost) == 0.0f, "cuts do not raise gain");
}

static bool is_passthrough(const biquad::Coefficients &c) {
  return c.b0 == 1.0 && c.b1 == 0.0 && c.b2 == 0.0 && c.a1 == 0.0 && c.a2 == 0.0;
}

static double largest_coefficient(const biquad::Coefficients &c) {
  return std::fmax(std::fmax(std::fmax(std::fabs(c.b0), std::fabs(c.b1)), std::fmax(std::fabs(c.b2), std::fabs(c.a1))),
                   std::fabs(c.a2));
}

// every band over the allowed setting ranges must fit Q5.27 (+-16) with some margin
static void test_coefficients_fit_q5_27() {
  static const float RATES[] = {44100.0f, 48000.0f, 96000.0f};
  static const float FREQS[] = {20.0f, 50.0f, 200.0f, 1000.0f, 5000.0f, 12000.0f, 20000.0f};
  static const float GAINS[] = {-15.0f, -6.0f, 6.0f, 15.0f};
  static const float QS[] = {0.1f, 0.707f, 3.0f, 10.0f};
  static const float LOW_FREQS[] = {15.0f, 30.0f, 60.0f, 120.0f, 200.0f};
  static const float LOW_QS[] = {0.3f, 0.707f, 2.0f};
  const crossover::LoudnessBoost max_boost{crossover::LOUDNESS_BASS_MAX_DB, crossover::LOUDNESS_TREBLE_MAX_DB};

  double worst = 0.0;
  auto check_chain = [&](const crossover::Settings &s, float fs) {
    for (uint8_t role = 0; role < crossover::NUMBER_ROLES; role++) {
      for (uint8_t band = 0; band < crossover::NUMBER_BANDS; band++) {
        const double m = largest_coefficient(
            crossover::band_coefficients(s, static_cast<crossover::DriverRole>(role), band, fs, max_boost));
        if (m > worst) worst = m;
      }
    }
  };

  for (float fs : RATES) {
    for (float f : FREQS) {
      for (float g : GAINS) {
        for (float q : QS) {
          for (uint8_t type = 0; type < 3; type++) {
            crossover::Settings s;
            s.driver[crossover::ROLE_MID].gain_db = crossover::DRIVER_GAIN_MAX_DB;
            s.driver[crossover::ROLE_MID].delay_mm = 50.0f;
            s.driver[crossover::ROLE_MID].peq[0] = {f, g, q, static_cast<crossover::PeqType>(type)};
            s.driver[crossover::ROLE_TWEETER].peq[0] = {f, g, q, static_cast<crossover::PeqType>(type)};
            s.air_gain_db = (g > 0.0f) ? crossover::AIR_GAIN_MAX_DB : -crossover::AIR_GAIN_MAX_DB;
            s.notch_frequency = std::fmax(f, crossover::NOTCH_FREQUENCY_MIN);
            s.notch_gain_db = crossover::NOTCH_GAIN_MIN_DB;
            s.notch_q = (q < crossover::NOTCH_Q_MIN) ? crossover::NOTCH_Q_MIN : q * 2.0f;
            s.baffle_width_cm = crossover::BAFFLE_WIDTH_MIN_CM;
            check_chain(s, fs);
          }
        }
      }
    }
    for (float f0 : LOW_FREQS) {
      for (float fp : LOW_FREQS) {
        for (float q0 : LOW_QS) {
          for (float qp : LOW_QS) {
            crossover::Settings s;
            s.linkwitz_f0 = f0;
            s.linkwitz_fp = fp;
            s.linkwitz_q0 = q0;
            s.linkwitz_qp = qp;
            s.subsonic_frequency = fp < crossover::SUBSONIC_FREQUENCY_MIN ? crossover::SUBSONIC_FREQUENCY_MIN : fp / 2.0f;
            check_chain(s, fs);
          }
        }
      }
    }
    for (uint8_t filter = 0; filter < crossover::NUMBER_FILTERS; filter++) {
      for (float f : {crossover::FREQUENCY_MIN, 2500.0f, crossover::FREQUENCY_MAX}) {
        crossover::Settings s;
        s.filter = static_cast<crossover::CrossoverFilter>(filter);
        s.frequency = f;
        check_chain(s, fs);
      }
    }
  }
  CHECK(worst < 15.9, "largest coefficient %.3f must stay inside Q5.27", worst);
}

// the highest notch must still be below Nyquist at the lowest DSP rate
static void test_notch_below_nyquist() {
  const crossover::LoudnessBoost no_boost;
  crossover::Settings s;
  s.notch_frequency = crossover::NOTCH_FREQUENCY_MAX;
  s.notch_gain_db = -6.0f;
  const biquad::Coefficients c = crossover::band_coefficients(s, crossover::ROLE_TWEETER, crossover::BAND_NOTCH, 48000.0f, no_boost);
  CHECK(!is_passthrough(c), "notch at %.0fHz is active at 48kHz", crossover::NOTCH_FREQUENCY_MAX);
  CHECK(near(db(response(c, crossover::NOTCH_FREQUENCY_MAX, 48000.0)), -6.0, 0.1), "notch at max frequency cuts 6dB");
}

// the Linkwitz boost rises towards DC, the peak search must find it and compensation must cancel it
static void test_linkwitz_peak_search() {
  const crossover::LoudnessBoost no_boost;
  for (float fs : {48000.0f, 96000.0f}) {
    crossover::Settings s;
    s.linkwitz_f0 = 60.0f;
    s.linkwitz_q0 = 0.707f;
    s.linkwitz_fp = 30.0f;
    s.linkwitz_qp = 0.5f;
    const double dc_gain = 20.0 * std::log10((60.0 / 30.0) * (60.0 / 30.0));
    const float peak = crossover::chain_peak_db(s, crossover::ROLE_MID, fs, no_boost, false);
    CHECK(near(peak, dc_gain, 0.5), "Linkwitz peak %.2fdB vs DC gain %.2fdB at %.0fHz", peak, dc_gain, fs);

    s.driver[crossover::ROLE_MID].peq[0] = {1000.0f, 6.0f, 2.0f, crossover::PEQ_PEAKING};
    s.headroom_compensation = true;
    const float total = crossover::chain_peak_db(s, crossover::ROLE_MID, fs, no_boost, true);
    CHECK(total <= 0.05f, "compensated Linkwitz chain peaks at %.2fdB at %.0fHz", total, fs);
  }
}

// poles must stay inside the unit circle after rounding to Q5.27
static void test_low_frequency_poles_stable_after_encoding() {
  const crossover::LoudnessBoost no_boost;
  crossover::Settings s;
  s.subsonic_frequency = crossover::SUBSONIC_FREQUENCY_MIN;
  s.linkwitz_f0 = crossover::LINKWITZ_FREQUENCY_MAX;
  s.linkwitz_fp = crossover::LINKWITZ_FREQUENCY_MIN;
  s.linkwitz_qp = crossover::LINKWITZ_Q_MAX;
  for (uint8_t band : {crossover::BAND_SUBSONIC, crossover::BAND_LINKWITZ}) {
    uint8_t bytes[biquad::ENCODED_SIZE];
    biquad::encode(crossover::band_coefficients(s, crossover::ROLE_MID, band, 96000.0f, no_boost), bytes);
    // A1 and A2 are stored negated
    const double a1 = -double(int32_t(word(bytes, 3))) / 134217728.0;
    const double a2 = -double(int32_t(word(bytes, 4))) / 134217728.0;
    CHECK(a2 < 1.0 && std::fabs(a1) < 1.0 + a2, "band %d poles stable after encoding (a1 %.9f a2 %.9f)", band + 1, a1, a2);
  }
}

int main() {
  test_lr4_2500hz_reference();
  test_crossover_responses();
  test_eq_filters();
  test_lr4_2500hz_reference_96k();
  test_register_format_matches_ppc3();
  test_allpass_delay();
  test_encoding();
  test_chain();
  test_loudness();
  test_speaker_compensation();
  test_linkwitz_and_headroom();
  test_coefficients_fit_q5_27();
  test_notch_below_nyquist();
  test_linkwitz_peak_search();
  test_low_frequency_poles_stable_after_encoding();

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
