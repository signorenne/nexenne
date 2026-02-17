/**
 * @file
 * @brief Deep tests for the nexenne::filter linear smoothers.
 *
 * Covers ema, sma, lowpass, highpass, biquad, butterworth, and fir.
 * Exact numeric values are asserted wherever they are computable in
 * closed form; floating-point comparisons use doctest::Approx.
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>

#include <nexenne/filter/filter.hpp>

namespace {

namespace flt = nexenne::filter;

// Each smoother satisfies the shared filter_like surface, for float and double.
static_assert(flt::filter_like<flt::ema<float>>);
static_assert(flt::filter_like<flt::ema<double>>);
static_assert(flt::filter_like<flt::sma<float, 4>>);
static_assert(flt::filter_like<flt::sma<double, 8>>);
static_assert(flt::filter_like<flt::lowpass<float>>);
static_assert(flt::filter_like<flt::lowpass<double>>);
static_assert(flt::filter_like<flt::highpass<float>>);
static_assert(flt::filter_like<flt::highpass<double>>);
static_assert(flt::filter_like<flt::biquad<float>>);
static_assert(flt::filter_like<flt::biquad<double>>);
static_assert(flt::filter_like<flt::butterworth<float, 1>>);
static_assert(flt::filter_like<flt::butterworth<double, 2>>);
static_assert(flt::filter_like<flt::fir<float, 4>>);
static_assert(flt::filter_like<flt::fir<double, 4>>);

// Defaults: value_type is double, sma window is 8.
static_assert(std::same_as<flt::ema<>::value_type, double>);
static_assert(std::same_as<flt::sma<>::value_type, double>);
static_assert(flt::sma<>::window_size == 8);
static_assert(flt::sma<double, 4>::window_size == 4);
static_assert(flt::fir<double, 6>::taps == 6);
static_assert(flt::butterworth<double, 3>::sections == 3);
static_assert(flt::butterworth<double, 3>::order == 6);

constexpr auto pi{std::numbers::pi};

TEST_CASE("nexenne::filter::ema construction and accessors") {
  auto f{flt::ema{0.25}};
  CHECK(f.alpha() == doctest::Approx(0.25));
  // value() is zero before the first push.
  CHECK(f.value() == doctest::Approx(0.0));
  f.alpha(0.5);
  CHECK(f.alpha() == doctest::Approx(0.5));
}

TEST_CASE("nexenne::filter::ema first sample seeds the output (no startup lag)") {
  auto f{flt::ema{0.5}};
  CHECK(f.push(10.0) == doctest::Approx(10.0));
  CHECK(f.value() == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::ema alpha=1 passes the input through unchanged") {
  auto f{flt::ema{1.0}};
  static_cast<void>(f.push(0.0));
  CHECK(f.push(42.0) == doctest::Approx(42.0));
  CHECK(f.push(-7.0) == doctest::Approx(-7.0));
  CHECK(f.push(3.5) == doctest::Approx(3.5));
}

TEST_CASE("nexenne::filter::ema alpha=0 holds the seeded value forever") {
  auto f{flt::ema{0.0}};
  CHECK(f.push(5.0) == doctest::Approx(5.0));  // first push seeds directly
  // alpha 0 means y[n] = 0*x + 1*y[n-1], so the value is frozen.
  CHECK(f.push(100.0) == doctest::Approx(5.0));
  CHECK(f.push(-50.0) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::ema exact recurrence y += alpha*(x - y)") {
  // alpha = 0.25, seed 4, then push 8 three times.
  // 4 -> 4 + .25*(8-4) = 5 -> 5 + .25*(8-5) = 5.75 -> 5.75 + .25*2.25 = 6.3125
  auto f{flt::ema{0.25}};
  CHECK(f.push(4.0) == doctest::Approx(4.0));
  CHECK(f.push(8.0) == doctest::Approx(5.0));
  CHECK(f.push(8.0) == doctest::Approx(5.75));
  CHECK(f.push(8.0) == doctest::Approx(6.3125));
}

TEST_CASE("nexenne::filter::ema step response converges to DC with monotonic approach") {
  auto f{flt::ema{0.1}};
  static_cast<void>(f.push(0.0));  // seed at 0
  auto prev{f.value()};
  for (auto i{0}; i < 200; ++i) {
    auto const y{f.push(100.0)};
    CHECK(y >= prev);   // monotonic, non-decreasing approach to 100
    CHECK(y <= 100.0);  // never overshoots a positive step
    prev = y;
  }
  CHECK(f.value() == doctest::Approx(100.0).epsilon(1e-6));
}

TEST_CASE("nexenne::filter::ema impulse response decays geometrically") {
  // alpha = 0.5: seed with the impulse, then zeros halve the value each step.
  auto f{flt::ema{0.5}};
  CHECK(f.push(1.0) == doctest::Approx(1.0));
  CHECK(f.push(0.0) == doctest::Approx(0.5));
  CHECK(f.push(0.0) == doctest::Approx(0.25));
  CHECK(f.push(0.0) == doctest::Approx(0.125));
  CHECK(f.push(0.0) == doctest::Approx(0.0625));
}

TEST_CASE("nexenne::filter::ema reset returns to a fresh filter state") {
  auto f{flt::ema{0.3}};
  static_cast<void>(f.push(9.0));
  static_cast<void>(f.push(3.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  // The next push reseeds directly, exactly like a fresh filter.
  auto fresh{flt::ema{0.3}};
  CHECK(f.push(7.0) == doctest::Approx(fresh.push(7.0)));
}

TEST_CASE("nexenne::filter::ema reset(initial) primes at a known value") {
  auto f{flt::ema{0.5}};
  f.reset(20.0);
  CHECK(f.value() == doctest::Approx(20.0));
  // Already primed, so the next push blends rather than seeds.
  CHECK(f.push(0.0) == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::ema float and double instantiations agree") {
  auto fd{flt::ema<double>{0.25}};
  auto ff{flt::ema<float>{0.25F}};
  static_cast<void>(fd.push(4.0));
  static_cast<void>(ff.push(4.0F));
  CHECK(static_cast<double>(ff.push(8.0F)) == doctest::Approx(fd.push(8.0)));
}

TEST_CASE("nexenne::filter::sma default state is empty") {
  auto f{flt::sma<double, 4>{}};
  CHECK(f.count() == 0);
  CHECK_FALSE(f.filled());
  CHECK(f.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::sma window-fill transient then steady state") {
  auto f{flt::sma<double, 4>{}};
  // During the fill the divisor is the running count, not N.
  CHECK(f.push(1.0) == doctest::Approx(1.0));  // 1/1
  CHECK(f.push(2.0) == doctest::Approx(1.5));  // (1+2)/2
  CHECK(f.push(3.0) == doctest::Approx(2.0));  // (1+2+3)/3
  CHECK(f.push(4.0) == doctest::Approx(2.5));  // (1+2+3+4)/4 — now full
  CHECK(f.filled());
  CHECK(f.count() == 4);
  CHECK(f.push(5.0) == doctest::Approx(3.5));  // (2+3+4+5)/4
  CHECK(f.push(6.0) == doctest::Approx(4.5));  // (3+4+5+6)/4
}

TEST_CASE("nexenne::filter::sma window size 1 is a passthrough") {
  auto f{flt::sma<double, 1>{}};
  CHECK(f.push(3.0) == doctest::Approx(3.0));
  CHECK(f.push(99.0) == doctest::Approx(99.0));
  CHECK(f.push(-4.0) == doctest::Approx(-4.0));
  CHECK(f.filled());
}

TEST_CASE("nexenne::filter::sma sliding window drops the oldest sample") {
  auto f{flt::sma<double, 3>{}};
  static_cast<void>(f.push(10.0));
  static_cast<void>(f.push(20.0));
  static_cast<void>(f.push(30.0));
  CHECK(f.value() == doctest::Approx(20.0));     // (10+20+30)/3
  CHECK(f.push(40.0) == doctest::Approx(30.0));  // (20+30+40)/3
  CHECK(f.push(50.0) == doctest::Approx(40.0));  // (30+40+50)/3
}

TEST_CASE("nexenne::filter::sma filled reports when the window is full") {
  auto f{flt::sma<double, 3>{}};
  CHECK_FALSE(f.filled());
  static_cast<void>(f.push(1.0));
  CHECK(f.count() == 1);
  static_cast<void>(f.push(2.0));
  CHECK_FALSE(f.filled());
  static_cast<void>(f.push(3.0));
  CHECK(f.filled());
  CHECK(f.count() == 3);
  static_cast<void>(f.push(4.0));
  CHECK(f.count() == 3);  // count saturates at N
}

TEST_CASE("nexenne::filter::sma step response converges exactly to the DC level") {
  auto f{flt::sma<double, 8>{}};
  for (auto i{0}; i < 8; ++i) {
    static_cast<void>(f.push(5.0));
  }
  CHECK(f.value() == doctest::Approx(5.0));  // a full window of 5 averages to 5
}

TEST_CASE("nexenne::filter::sma value() matches push() without advancing") {
  auto f{flt::sma<double, 3>{}};
  auto const y{f.push(6.0)};
  CHECK(f.value() == doctest::Approx(y));
  CHECK(f.value() == doctest::Approx(y));  // repeated reads do not change state
}

TEST_CASE("nexenne::filter::sma reset clears window and matches a fresh filter") {
  auto f{flt::sma<double, 4>{}};
  static_cast<void>(f.push(1.0));
  static_cast<void>(f.push(2.0));
  static_cast<void>(f.push(3.0));
  f.reset();
  CHECK(f.count() == 0);
  CHECK_FALSE(f.filled());
  CHECK(f.value() == doctest::Approx(0.0));
  auto fresh{flt::sma<double, 4>{}};
  CHECK(f.push(7.0) == doctest::Approx(fresh.push(7.0)));
}

TEST_CASE("nexenne::filter::sma float and double agree on the window average") {
  auto fd{flt::sma<double, 4>{}};
  auto ff{flt::sma<float, 4>{}};
  for (auto const x : {2.0, 4.0, 6.0, 8.0}) {
    static_cast<void>(fd.push(x));
    static_cast<void>(ff.push(static_cast<float>(x)));
  }
  CHECK(fd.value() == doctest::Approx(5.0));
  CHECK(static_cast<double>(ff.value()) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::lowpass alpha derives from cutoff and sample rate") {
  // alpha = dt / (RC + dt), dt = 1/fs, RC = 1/(2*pi*fc).
  auto lp{flt::lowpass{10.0, 1000.0}};
  auto const dt{1.0 / 1000.0};
  auto const rc{1.0 / (2.0 * pi * 10.0)};
  CHECK(lp.alpha() == doctest::Approx(dt / (rc + dt)));
  CHECK(lp.value() == doctest::Approx(0.0));  // unprimed
}

TEST_CASE("nexenne::filter::lowpass first sample seeds directly") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  CHECK(lp.push(3.0) == doctest::Approx(3.0));
}

TEST_CASE("nexenne::filter::lowpass DC gain is one (passes a constant)") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(lp.push(5.0));
  }
  CHECK(lp.value() == doctest::Approx(5.0).epsilon(1e-6));
}

TEST_CASE("nexenne::filter::lowpass step response is monotonic toward the input") {
  auto lp{flt::lowpass{50.0, 1000.0}};
  static_cast<void>(lp.push(0.0));
  auto prev{lp.value()};
  for (auto i{0}; i < 300; ++i) {
    auto const y{lp.push(1.0)};
    CHECK(y >= prev);
    CHECK(y <= 1.0);
    prev = y;
  }
  CHECK(lp.value() == doctest::Approx(1.0).epsilon(1e-4));
}

TEST_CASE("nexenne::filter::lowpass attenuates a fast alternating signal") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  // +1, -1, +1, -1 ... is the fastest representable signal (Nyquist).
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(lp.push(i % 2 == 0 ? 1.0 : -1.0));
  }
  CHECK(std::abs(lp.value()) < 0.1);
}

TEST_CASE("nexenne::filter::lowpass attenuates a high-frequency sine") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  for (auto i{0}; i < 500; ++i) {
    auto const x{std::sin(2.0 * pi * 200.0 * static_cast<double>(i) / 1000.0)};
    static_cast<void>(lp.push(x));
  }
  CHECK(std::abs(lp.value()) < 0.1);
}

TEST_CASE("nexenne::filter::lowpass cutoff() recomputes the coefficient") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  auto const a_low{lp.alpha()};
  lp.cutoff(100.0, 1000.0);
  // A higher cutoff yields a larger alpha (tracks faster).
  CHECK(lp.alpha() > a_low);
  auto const dt{1.0 / 1000.0};
  auto const rc{1.0 / (2.0 * pi * 100.0)};
  CHECK(lp.alpha() == doctest::Approx(dt / (rc + dt)));
}

TEST_CASE("nexenne::filter::lowpass reset matches a fresh filter and keeps the cutoff") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  auto const a{lp.alpha()};
  static_cast<void>(lp.push(7.0));
  static_cast<void>(lp.push(3.0));
  lp.reset();
  CHECK(lp.value() == doctest::Approx(0.0));
  CHECK(lp.alpha() == doctest::Approx(a));  // coefficient preserved
  auto fresh{flt::lowpass{10.0, 1000.0}};
  CHECK(lp.push(9.0) == doctest::Approx(fresh.push(9.0)));
}

TEST_CASE("nexenne::filter::lowpass float and double track the same step") {
  auto ld{flt::lowpass<double>{50.0, 1000.0}};
  auto lf{flt::lowpass<float>{50.0F, 1000.0F}};
  for (auto i{0}; i < 200; ++i) {
    static_cast<void>(ld.push(1.0));
    static_cast<void>(lf.push(1.0F));
  }
  CHECK(static_cast<double>(lf.value()) == doctest::Approx(ld.value()).epsilon(1e-4));
}

TEST_CASE("nexenne::filter::highpass alpha derives from cutoff and sample rate") {
  // alpha = RC / (RC + dt), the complement-style coefficient.
  auto hp{flt::highpass{10.0, 1000.0}};
  auto const dt{1.0 / 1000.0};
  auto const rc{1.0 / (2.0 * pi * 10.0)};
  CHECK(hp.alpha() == doctest::Approx(rc / (rc + dt)));
}

TEST_CASE("nexenne::filter::highpass first sample emits zero") {
  auto hp{flt::highpass{10.0, 1000.0}};
  CHECK(hp.push(42.0) == doctest::Approx(0.0));  // seeds input memory, outputs 0
  CHECK(hp.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::highpass blocks DC (constant input converges to zero)") {
  auto hp{flt::highpass{10.0, 1000.0}};
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(hp.push(5.0));  // pure DC
  }
  CHECK(std::abs(hp.value()) < 1e-3);
}

TEST_CASE("nexenne::filter::highpass exact difference equation on a step") {
  // y[n] = alpha*(y[n-1] + x[n] - x[n-1]); first push seeds x and emits 0.
  auto hp{flt::highpass{10.0, 1000.0}};
  auto const a{hp.alpha()};
  CHECK(hp.push(0.0) == doctest::Approx(0.0));
  // step to 1: y = a*(0 + 1 - 0) = a
  CHECK(hp.push(1.0) == doctest::Approx(a));
  // hold at 1: y = a*(a + 1 - 1) = a^2
  CHECK(hp.push(1.0) == doctest::Approx(a * a));
  // hold at 1: y = a*(a^2 + 0) = a^3
  CHECK(hp.push(1.0) == doctest::Approx(a * a * a));
}

TEST_CASE("nexenne::filter::highpass passes a fast alternating signal") {
  auto hp{flt::highpass{10.0, 1000.0}};
  for (auto i{0}; i < 200; ++i) {
    static_cast<void>(hp.push(i % 2 == 0 ? 1.0 : -1.0));
  }
  // With alpha near 1 the alternating component survives with near-unit swing.
  CHECK(std::abs(hp.value()) > 0.8);
}

TEST_CASE("nexenne::filter::highpass impulse response onset and decay") {
  auto hp{flt::highpass{10.0, 1000.0}};
  auto const a{hp.alpha()};
  CHECK(hp.push(0.0) == doctest::Approx(0.0));  // seed
  // impulse: x goes 0 -> 1, y = a*(0 + 1 - 0) = a
  CHECK(hp.push(1.0) == doctest::Approx(a));
  // back to 0: y = a*(a + 0 - 1) = a*(a - 1) (negative undershoot)
  CHECK(hp.push(0.0) == doctest::Approx(a * (a - 1.0)));
  // next zero: y = a*(prev + 0 - 0) = a*prev
  CHECK(hp.push(0.0) == doctest::Approx(a * (a * (a - 1.0))));
}

TEST_CASE("nexenne::filter::highpass reset matches a fresh filter and keeps the cutoff") {
  auto hp{flt::highpass{10.0, 1000.0}};
  auto const a{hp.alpha()};
  static_cast<void>(hp.push(4.0));
  static_cast<void>(hp.push(9.0));
  hp.reset();
  CHECK(hp.value() == doctest::Approx(0.0));
  CHECK(hp.alpha() == doctest::Approx(a));
  auto fresh{flt::highpass{10.0, 1000.0}};
  CHECK(hp.push(1.0) == doctest::Approx(fresh.push(1.0)));  // both seed -> 0
  CHECK(hp.push(2.0) == doctest::Approx(fresh.push(2.0)));
}

TEST_CASE("nexenne::filter::highpass cutoff() recomputes the coefficient") {
  auto hp{flt::highpass{10.0, 1000.0}};
  auto const a_low{hp.alpha()};
  hp.cutoff(100.0, 1000.0);
  // A higher cutoff lowers the high-pass alpha.
  CHECK(hp.alpha() < a_low);
}

TEST_CASE("nexenne::filter::highpass lowpass complementary sum reconstructs the input") {
  // For these first-order designs lp.value() + hp.value() tracks the input
  // closely once both have settled on a slowly varying signal.
  auto lp{flt::lowpass{50.0, 1000.0}};
  auto hp{flt::highpass{50.0, 1000.0}};
  auto const x{7.0};
  for (auto i{0}; i < 2000; ++i) {
    static_cast<void>(lp.push(x));
    static_cast<void>(hp.push(x));
  }
  // Low-pass holds the DC, high-pass has rejected it: their sum is the input.
  CHECK(lp.value() + hp.value() == doctest::Approx(x).epsilon(1e-3));
}

TEST_CASE("nexenne::filter::highpass float and double agree on a step") {
  auto hd{flt::highpass<double>{10.0, 1000.0}};
  auto hf{flt::highpass<float>{10.0F, 1000.0F}};
  static_cast<void>(hd.push(0.0));
  static_cast<void>(hf.push(0.0F));
  CHECK(static_cast<double>(hf.push(1.0F)) == doctest::Approx(hd.push(1.0)).epsilon(1e-5));
}

TEST_CASE("nexenne::filter::biquad default constructs a passthrough") {
  auto bq{flt::biquad<double>{}};
  auto const c{bq.coefs()};
  CHECK(c.b0 == doctest::Approx(1.0));
  CHECK(c.b1 == doctest::Approx(0.0));
  CHECK(c.b2 == doctest::Approx(0.0));
  CHECK(c.a1 == doctest::Approx(0.0));
  CHECK(c.a2 == doctest::Approx(0.0));
  CHECK(bq.push(3.14) == doctest::Approx(3.14));
  CHECK(bq.push(-1.0) == doctest::Approx(-1.0));
}

TEST_CASE("nexenne::filter::biquad explicit coefficients drive the difference equation") {
  // y[n] = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2.
  // Pure feedforward gain of 2: y[n] = 2*x[n].
  auto bq{flt::biquad<double>{flt::biquad<double>::coefficients{.b0 = 2.0}}};
  CHECK(bq.push(3.0) == doctest::Approx(6.0));
  CHECK(bq.push(-4.0) == doctest::Approx(-8.0));
}

TEST_CASE("nexenne::filter::biquad coefs() setter replaces the coefficient set") {
  auto bq{flt::biquad<double>{}};
  bq.coefs(flt::biquad<double>::coefficients{.b0 = 0.0, .b1 = 1.0});
  // y[n] = x[n-1]: a one-sample delay.
  CHECK(bq.push(5.0) == doctest::Approx(0.0));  // x1 still 0
  CHECK(bq.push(0.0) == doctest::Approx(5.0));  // now x1 == 5
}

TEST_CASE("nexenne::filter::biquad make_lowpass matches RBJ cookbook coefficients") {
  auto const fc{50.0};
  auto const fs{1000.0};
  auto const q{0.7071};
  auto const w0{2.0 * pi * fc / fs};
  auto const sw{std::sin(w0)};
  auto const cw{std::cos(w0)};
  auto const a{sw / (2.0 * q)};
  auto const a0{1.0 + a};
  auto const bq{flt::biquad<double>::make_lowpass(fc, fs, q)};
  auto const c{bq.coefs()};
  CHECK(c.b0 == doctest::Approx(((1.0 - cw) / 2.0) / a0));
  CHECK(c.b1 == doctest::Approx((1.0 - cw) / a0));
  CHECK(c.b2 == doctest::Approx(((1.0 - cw) / 2.0) / a0));
  CHECK(c.a1 == doctest::Approx((-2.0 * cw) / a0));
  CHECK(c.a2 == doctest::Approx((1.0 - a) / a0));
}

TEST_CASE("nexenne::filter::biquad lowpass DC gain is one") {
  // Analytic DC gain: (b0+b1+b2)/(1+a1+a2) == 1 for the cookbook low-pass.
  auto const bq{flt::biquad<double>::make_lowpass(50.0, 1000.0)};
  auto const c{bq.coefs()};
  CHECK((c.b0 + c.b1 + c.b2) / (1.0 + c.a1 + c.a2) == doctest::Approx(1.0));
  // And empirically: a sustained DC input settles at the input level.
  auto run{bq};
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(run.push(1.0));
  }
  CHECK(run.value() == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("nexenne::filter::biquad highpass matches RBJ cookbook and blocks DC") {
  auto const fc{50.0};
  auto const fs{1000.0};
  auto const q{0.7071};
  auto const w0{2.0 * pi * fc / fs};
  auto const cw{std::cos(w0)};
  auto const sw{std::sin(w0)};
  auto const a{sw / (2.0 * q)};
  auto const a0{1.0 + a};
  auto bq{flt::biquad<double>::make_highpass(fc, fs, q)};
  auto const c{bq.coefs()};
  CHECK(c.b0 == doctest::Approx(((1.0 + cw) / 2.0) / a0));
  CHECK(c.b1 == doctest::Approx((-(1.0 + cw)) / a0));
  CHECK(c.b2 == doctest::Approx(((1.0 + cw) / 2.0) / a0));
  // DC gain of a high-pass is zero: b0+b1+b2 == 0.
  CHECK(c.b0 + c.b1 + c.b2 == doctest::Approx(0.0));
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(bq.push(1.0));
  }
  CHECK(std::abs(bq.value()) < 1e-3);
}

TEST_CASE("nexenne::filter::biquad bandpass matches RBJ cookbook and blocks DC") {
  auto const fc{100.0};
  auto const fs{1000.0};
  auto const q{1.0};
  auto const w0{2.0 * pi * fc / fs};
  auto const sw{std::sin(w0)};
  auto const cw{std::cos(w0)};
  auto const a{sw / (2.0 * q)};
  auto const a0{1.0 + a};
  auto const bq{flt::biquad<double>::make_bandpass(fc, fs, q)};
  auto const c{bq.coefs()};
  CHECK(c.b0 == doctest::Approx((sw / 2.0) / a0));
  CHECK(c.b1 == doctest::Approx(0.0));
  CHECK(c.b2 == doctest::Approx(-(sw / 2.0) / a0));
  CHECK(c.a1 == doctest::Approx((-2.0 * cw) / a0));
  CHECK(c.a2 == doctest::Approx((1.0 - a) / a0));
  // DC gain is zero (b0+b1+b2 == 0) for a band-pass.
  CHECK(c.b0 + c.b1 + c.b2 == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::biquad notch matches RBJ cookbook with unity DC gain") {
  auto const fc{100.0};
  auto const fs{1000.0};
  auto const q{5.0};
  auto const w0{2.0 * pi * fc / fs};
  auto const sw{std::sin(w0)};
  auto const cw{std::cos(w0)};
  auto const a{sw / (2.0 * q)};
  auto const a0{1.0 + a};
  auto const bq{flt::biquad<double>::make_notch(fc, fs, q)};
  auto const c{bq.coefs()};
  CHECK(c.b0 == doctest::Approx(1.0 / a0));
  CHECK(c.b1 == doctest::Approx((-2.0 * cw) / a0));
  CHECK(c.b2 == doctest::Approx(1.0 / a0));
  CHECK(c.a1 == doctest::Approx((-2.0 * cw) / a0));
  CHECK(c.a2 == doctest::Approx((1.0 - a) / a0));
  // A notch passes DC unchanged: DC gain == 1.
  CHECK((c.b0 + c.b1 + c.b2) / (1.0 + c.a1 + c.a2) == doctest::Approx(1.0));
}

TEST_CASE("nexenne::filter::biquad notch kills its center frequency") {
  auto bq{flt::biquad<double>::make_notch(100.0, 1000.0, 5.0)};
  for (auto i{0}; i < 1000; ++i) {
    auto const x{std::sin(2.0 * pi * 100.0 * static_cast<double>(i) / 1000.0)};
    static_cast<void>(bq.push(x));
  }
  CHECK(std::abs(bq.value()) < 0.05);
}

TEST_CASE("nexenne::filter::biquad notch passes a far-off frequency") {
  // A tone well away from the notch centre survives near unity amplitude.
  auto bq{flt::biquad<double>::make_notch(100.0, 1000.0, 5.0)};
  auto peak{0.0};
  for (auto i{0}; i < 2000; ++i) {
    auto const x{std::sin(2.0 * pi * 5.0 * static_cast<double>(i) / 1000.0)};
    auto const y{bq.push(x)};
    if (i > 1000) {
      peak = std::max(peak, std::abs(y));
    }
  }
  CHECK(peak > 0.9);
}

TEST_CASE("nexenne::filter::biquad reset clears both delay elements") {
  auto bq{flt::biquad<double>::make_lowpass(50.0, 1000.0)};
  for (auto i{0}; i < 50; ++i) {
    static_cast<void>(bq.push(1.0));
  }
  bq.reset();
  CHECK(bq.value() == doctest::Approx(0.0));
  // After reset, the first output equals b0*x with all delays zero.
  auto const c{bq.coefs()};
  CHECK(bq.push(1.0) == doctest::Approx(c.b0));
  // Coefficients are preserved across reset.
  auto fresh{flt::biquad<double>::make_lowpass(50.0, 1000.0)};
  CHECK(bq.coefs().b0 == doctest::Approx(fresh.coefs().b0));
}

TEST_CASE("nexenne::filter::biquad float and double lowpass DC gains both unity") {
  auto bd{flt::biquad<double>::make_lowpass(50.0, 1000.0)};
  auto bf{flt::biquad<float>::make_lowpass(50.0F, 1000.0F)};
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(bd.push(1.0));
    static_cast<void>(bf.push(1.0F));
  }
  CHECK(bd.value() == doctest::Approx(1.0).epsilon(1e-3));
  CHECK(static_cast<double>(bf.value()) == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("nexenne::filter::butterworth default sections pass input unchanged") {
  auto f{flt::butterworth<double, 2>{}};
  CHECK(f.push(3.14) == doctest::Approx(3.14));
  CHECK(f.push(-2.0) == doctest::Approx(-2.0));
}

TEST_CASE("nexenne::filter::butterworth order-2 lowpass has unity DC gain") {
  auto f{flt::butterworth<double, 1>{}};
  f.design_low_pass(100.0, 1000.0);
  for (auto i{0}; i < 400; ++i) {
    static_cast<void>(f.push(1.0));
  }
  CHECK(f.value() == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("nexenne::filter::butterworth order-4 lowpass per-section Q is textbook") {
  // Order-4 (2 sections): Q_k = 1 / (2*sin((2k+1)*pi/(4N))), N=2.
  // The biquad low-pass DC gain is 1 per section regardless of Q, so the
  // cascade gain is also 1. We verify the analytic Q values feed through by
  // reconstructing the expected per-section coefficients and matching DC gain.
  auto f{flt::butterworth<double, 2>{}};
  f.design_low_pass(100.0, 1000.0);
  for (auto i{0}; i < 600; ++i) {
    static_cast<void>(f.push(1.0));
  }
  CHECK(f.value() == doctest::Approx(1.0).epsilon(1e-3));

  // The textbook per-section Q values for an order-4 Butterworth.
  auto const q0{1.0 / (2.0 * std::sin(1.0 * pi / 8.0))};  // ~1.30656
  auto const q1{1.0 / (2.0 * std::sin(3.0 * pi / 8.0))};  // ~0.54120
  CHECK(q0 == doctest::Approx(1.3065629648));
  CHECK(q1 == doctest::Approx(0.5411961001));
  // Build a single biquad with each Q at the same corner; cascade DC gain == 1.
  auto s0{flt::biquad<double>::make_lowpass(100.0, 1000.0, q0)};
  auto s1{flt::biquad<double>::make_lowpass(100.0, 1000.0, q1)};
  auto const c0{s0.coefs()};
  auto const c1{s1.coefs()};
  CHECK((c0.b0 + c0.b1 + c0.b2) / (1.0 + c0.a1 + c0.a2) == doctest::Approx(1.0));
  CHECK((c1.b0 + c1.b1 + c1.b2) / (1.0 + c1.a1 + c1.a2) == doctest::Approx(1.0));
  // Feeding the same DC through the two reference sections also yields 1.
  for (auto i{0}; i < 600; ++i) {
    static_cast<void>(s1.push(s0.push(1.0)));
  }
  CHECK(s1.value() == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("nexenne::filter::butterworth maximally-flat lowpass attenuates above cutoff") {
  auto f{flt::butterworth<double, 2>{}};
  f.design_low_pass(10.0, 1000.0);
  for (auto i{0}; i < 600; ++i) {
    auto const x{std::sin(2.0 * pi * 200.0 * static_cast<double>(i) / 1000.0)};
    static_cast<void>(f.push(x));
  }
  CHECK(std::abs(f.value()) < 0.01);
}

TEST_CASE("nexenne::filter::butterworth highpass design removes DC") {
  auto f{flt::butterworth<double, 2>{}};
  f.design_high_pass(50.0, 1000.0);
  for (auto i{0}; i < 600; ++i) {
    static_cast<void>(f.push(1.0));
  }
  CHECK(std::abs(f.value()) < 1e-3);
}

TEST_CASE("nexenne::filter::butterworth reset clears state but preserves coefficients") {
  auto f{flt::butterworth<double, 1>{}};
  f.design_low_pass(100.0, 1000.0);
  for (auto i{0}; i < 100; ++i) {
    static_cast<void>(f.push(1.0));
  }
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  for (auto i{0}; i < 400; ++i) {
    static_cast<void>(f.push(1.0));
  }
  CHECK(f.value() == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("nexenne::filter::butterworth float lowpass also has unity DC gain") {
  auto f{flt::butterworth<float, 2>{}};
  f.design_low_pass(50.0F, 1000.0F);
  for (auto i{0}; i < 600; ++i) {
    static_cast<void>(f.push(1.0F));
  }
  CHECK(static_cast<double>(f.value()) == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("nexenne::filter::fir default-constructed filter outputs zero") {
  auto f{flt::fir<double, 4>{}};
  CHECK(f.push(123.0) == doctest::Approx(0.0));
  CHECK(f.push(-7.0) == doctest::Approx(0.0));
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
}

TEST_CASE("nexenne::filter::fir matches a hand-computed convolution (index 0 == newest)") {
  auto const coeffs{std::array<double, 3>{0.5, 0.25, 0.25}};
  auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};
  CHECK(f.push(1.0) == doctest::Approx(0.5));   // 0.5*1
  CHECK(f.push(2.0) == doctest::Approx(1.25));  // 0.5*2 + 0.25*1
  CHECK(f.push(3.0) == doctest::Approx(2.25));  // 0.5*3 + 0.25*2 + 0.25*1
  CHECK(f.push(4.0) == doctest::Approx(3.25));  // 0.5*4 + 0.25*3 + 0.25*2
  CHECK(f.push(5.0) == doctest::Approx(4.25));  // 0.5*5 + 0.25*4 + 0.25*3
}

TEST_CASE("nexenne::filter::fir impulse response reproduces the kernel in order") {
  auto const coeffs{std::array<double, 3>{2.0, -1.0, 0.5}};
  auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};
  CHECK(f.push(1.0) == doctest::Approx(2.0));   // tap 0
  CHECK(f.push(0.0) == doctest::Approx(-1.0));  // tap 1
  CHECK(f.push(0.0) == doctest::Approx(0.5));   // tap 2
  CHECK(f.push(0.0) == doctest::Approx(0.0));   // fully shifted out
}

TEST_CASE("nexenne::filter::fir DC gain equals the sum of taps (step response)") {
  // Constant input settles at sum(coeffs) once the history fills.
  auto const coeffs{std::array<double, 4>{0.1, 0.2, 0.3, 0.4}};  // sum 1.0
  auto f{flt::fir<double, 4>{std::span<double const, 4>{coeffs}}};
  for (auto i{0}; i < 4; ++i) {
    static_cast<void>(f.push(2.0));
  }
  CHECK(f.value() == doctest::Approx(2.0));  // 2.0 * sum(coeffs) == 2.0
}

TEST_CASE("nexenne::filter::fir symmetric (linear-phase) coefficients") {
  // Symmetric 3-tap kernel {1, 2, 1} normalised. A symmetric impulse response
  // walks out symmetrically.
  auto const coeffs{std::array<double, 3>{0.25, 0.5, 0.25}};
  auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};
  CHECK(f.push(1.0) == doctest::Approx(0.25));
  CHECK(f.push(0.0) == doctest::Approx(0.5));
  CHECK(f.push(0.0) == doctest::Approx(0.25));  // mirror of the first tap
  CHECK(f.push(0.0) == doctest::Approx(0.0));
  // Coefficients are palindromic.
  auto const& got{f.coefficients()};
  CHECK(got[0] == doctest::Approx(got[2]));
}

TEST_CASE("nexenne::filter::fir coefficients() setter changes the response") {
  auto f{flt::fir<double, 2>{}};
  auto const coeffs{std::array<double, 2>{1.0, 1.0}};  // running sum of 2
  f.coefficients(std::span<double const, 2>{coeffs});
  CHECK(f.push(3.0) == doctest::Approx(3.0));  // 1*3 + 1*0
  CHECK(f.push(4.0) == doctest::Approx(7.0));  // 1*4 + 1*3
  CHECK(f.coefficients()[0] == doctest::Approx(1.0));
  CHECK(f.coefficients()[1] == doctest::Approx(1.0));
}

TEST_CASE("nexenne::filter::fir value mirrors the last push; reset clears history") {
  auto const coeffs{std::array<double, 2>{1.0, 1.0}};
  auto f{flt::fir<double, 2>{std::span<double const, 2>{coeffs}}};
  auto const y{f.push(5.0)};
  CHECK(f.value() == doctest::Approx(y));
  CHECK(f.push(6.0) == doctest::Approx(11.0));  // 6 + 5
  CHECK(f.value() == doctest::Approx(11.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  // History is silent, so coefficients are preserved across reset.
  CHECK(f.coefficients()[0] == doctest::Approx(1.0));
  CHECK(f.push(2.0) == doctest::Approx(2.0));
}

TEST_CASE("nexenne::filter::fir reset matches a fresh filter") {
  auto const coeffs{std::array<double, 3>{0.5, 0.25, 0.25}};
  auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};
  static_cast<void>(f.push(7.0));
  static_cast<void>(f.push(2.0));
  f.reset();
  auto fresh{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};
  CHECK(f.push(4.0) == doctest::Approx(fresh.push(4.0)));
  CHECK(f.push(1.0) == doctest::Approx(fresh.push(1.0)));
}

TEST_CASE("nexenne::filter::fir float and double agree on a known convolution") {
  auto const cd{std::array<double, 3>{0.5, 0.25, 0.25}};
  auto const cf{std::array<float, 3>{0.5F, 0.25F, 0.25F}};
  auto fd{flt::fir<double, 3>{std::span<double const, 3>{cd}}};
  auto ff{flt::fir<float, 3>{std::span<float const, 3>{cf}}};
  for (auto const x : {1.0, 2.0, 3.0, 4.0}) {
    static_cast<void>(fd.push(x));
    static_cast<void>(ff.push(static_cast<float>(x)));
  }
  CHECK(static_cast<double>(ff.value()) == doctest::Approx(fd.value()));
}

TEST_CASE("nexenne::filter::ema propagates a NaN input through the blend") {
  auto f{flt::ema{0.5}};
  static_cast<void>(f.push(1.0));
  auto const y{f.push(std::numeric_limits<double>::quiet_NaN())};
  CHECK(std::isnan(y));  // documented: no special NaN handling
}

TEST_CASE("nexenne::filter::sma very long constant run stays at steady state") {
  auto f{flt::sma<double, 8>{}};
  for (auto i{0}; i < 100000; ++i) {
    static_cast<void>(f.push(3.0));
  }
  CHECK(f.value() == doctest::Approx(3.0));  // no drift from incremental sum
}

TEST_CASE("nexenne::filter::lowpass very long constant run holds DC exactly") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  for (auto i{0}; i < 100000; ++i) {
    static_cast<void>(lp.push(2.5));
  }
  CHECK(lp.value() == doctest::Approx(2.5).epsilon(1e-9));
}

}  // namespace
