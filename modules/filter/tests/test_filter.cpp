/**
 * @file
 * @brief Tests for the nexenne::filter module (all filters, one suite).
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <span>

#include <nexenne/filter/filter.hpp>

namespace {

namespace flt = nexenne::filter;

static_assert(flt::filter_like<flt::ema<double>>);
static_assert(flt::filter_like<flt::sma<double, 4>>);
static_assert(flt::filter_like<flt::lowpass<double>>);
static_assert(flt::filter_like<flt::highpass<double>>);
static_assert(flt::filter_like<flt::biquad<double>>);
static_assert(flt::filter_like<flt::median<double, 3>>);
static_assert(flt::filter_like<flt::kalman<double>>);
static_assert(flt::filter_like<flt::slew<double>>);

TEST_CASE("ema: first sample seeds the output") {
  auto f{flt::ema{0.5}};
  CHECK(f.push(10.0) == doctest::Approx(10.0));
}

TEST_CASE("ema: smooths toward the input over time") {
  auto f{flt::ema{0.1}};
  static_cast<void>(f.push(0.0));
  for (auto i{0}; i < 100; ++i) {
    static_cast<void>(f.push(100.0));
  }
  CHECK(f.value() == doctest::Approx(100.0).epsilon(0.01));
}

TEST_CASE("ema: alpha=1 tracks instantly") {
  auto f{flt::ema{1.0}};
  static_cast<void>(f.push(0.0));
  CHECK(f.push(42.0) == doctest::Approx(42.0));
}

TEST_CASE("sma: averages the window") {
  auto f{flt::sma<double, 4>{}};
  static_cast<void>(f.push(1.0));
  static_cast<void>(f.push(2.0));
  static_cast<void>(f.push(3.0));
  CHECK(f.push(4.0) == doctest::Approx(2.5));
}

TEST_CASE("sma: sliding window drops oldest") {
  auto f{flt::sma<double, 3>{}};
  static_cast<void>(f.push(10.0));
  static_cast<void>(f.push(20.0));
  static_cast<void>(f.push(30.0));
  CHECK(f.value() == doctest::Approx(20.0));     // (10+20+30)/3
  CHECK(f.push(40.0) == doctest::Approx(30.0));  // (20+30+40)/3
}

TEST_CASE("sma: filled reports when window is full") {
  auto f{flt::sma<double, 3>{}};
  CHECK_FALSE(f.filled());
  static_cast<void>(f.push(1.0));
  static_cast<void>(f.push(2.0));
  CHECK_FALSE(f.filled());
  static_cast<void>(f.push(3.0));
  CHECK(f.filled());
}

TEST_CASE("lowpass: attenuates a high-frequency signal") {
  // 10 Hz cutoff, 1000 Hz sample rate. Feed a 200 Hz sine.
  // After settling, amplitude should be heavily attenuated.
  auto lp{flt::lowpass{10.0, 1000.0}};
  for (auto i{0}; i < 500; ++i) {
    auto const x{std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(i) / 1000.0)};
    static_cast<void>(lp.push(x));
  }
  // After 500 samples of a 200 Hz tone through a 10 Hz LP,
  // the output should be near zero (heavy attenuation).
  CHECK(std::abs(lp.value()) < 0.1);
}

TEST_CASE("lowpass: passes a DC signal unchanged") {
  auto lp{flt::lowpass{10.0, 1000.0}};
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(lp.push(5.0));
  }
  CHECK(lp.value() == doctest::Approx(5.0).epsilon(0.001));
}

TEST_CASE("highpass: removes DC offset") {
  auto hp{flt::highpass{10.0, 1000.0}};
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(hp.push(5.0));  // pure DC
  }
  CHECK(std::abs(hp.value()) < 0.01);
}

TEST_CASE("biquad: lowpass design passes DC") {
  auto bq{flt::biquad<double>::make_lowpass(50.0, 1000.0)};
  for (auto i{0}; i < 200; ++i) {
    static_cast<void>(bq.push(1.0));
  }
  CHECK(bq.value() == doctest::Approx(1.0).epsilon(0.01));
}

TEST_CASE("biquad: notch attenuates center frequency") {
  auto bq{flt::biquad<double>::make_notch(100.0, 1000.0, 5.0)};
  // Feed a 100 Hz sine - the notch should kill it.
  for (auto i{0}; i < 500; ++i) {
    auto const x{std::sin(2.0 * std::numbers::pi * 100.0 * static_cast<double>(i) / 1000.0)};
    static_cast<void>(bq.push(x));
  }
  CHECK(std::abs(bq.value()) < 0.05);
}

TEST_CASE("median: rejects a single spike") {
  auto f{flt::median<double, 3>{}};
  static_cast<void>(f.push(10.0));
  static_cast<void>(f.push(10.0));
  static_cast<void>(f.push(10.0));
  CHECK(f.push(1000.0) == doctest::Approx(10.0));
  CHECK(f.push(10.0) == doctest::Approx(10.0));
}

TEST_CASE("median: tracks a step change after N/2+1 samples") {
  auto f{flt::median<double, 5>{}};
  for (auto i{0}; i < 5; ++i)
    static_cast<void>(f.push(0.0));
  static_cast<void>(f.push(100.0));
  static_cast<void>(f.push(100.0));
  auto const v{f.push(100.0)};  // 3 of 5 are now 100
  CHECK(v == doctest::Approx(100.0));
}

TEST_CASE("kalman: converges to a constant measurement") {
  auto kf{flt::kalman{0.01, 0.1}};
  for (auto i{0}; i < 50; ++i) {
    static_cast<void>(kf.push(42.0));
  }
  CHECK(kf.value() == doctest::Approx(42.0).epsilon(0.01));
}

TEST_CASE("kalman: tracks a step change") {
  auto kf{flt::kalman{0.1, 0.5}};
  for (auto i{0}; i < 20; ++i)
    static_cast<void>(kf.push(0.0));
  for (auto i{0}; i < 50; ++i)
    static_cast<void>(kf.push(100.0));
  CHECK(kf.value() == doctest::Approx(100.0).epsilon(1.0));
}

TEST_CASE("complementary: blends two sensors") {
  auto cf{flt::complementary{0.98}};
  auto const y{cf.push(10.0, 9.5)};
  // 0.98 * 10 + 0.02 * 9.5 = 9.99
  CHECK(y == doctest::Approx(9.99).epsilon(0.001));
}

TEST_CASE("slew: limits rate of change") {
  auto f{flt::slew{5.0}};
  static_cast<void>(f.push(0.0));
  CHECK(f.push(100.0) == doctest::Approx(5.0));
  CHECK(f.push(100.0) == doctest::Approx(10.0));
  CHECK(f.push(100.0) == doctest::Approx(15.0));
}

TEST_CASE("slew: allows change within rate") {
  auto f{flt::slew{10.0}};
  static_cast<void>(f.push(0.0));
  CHECK(f.push(5.0) == doctest::Approx(5.0));
}

TEST_CASE("debounce: rejects single glitches") {
  auto f{flt::debounce<bool, 3>{}};
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));
  // Single glitch:
  CHECK(f.push(true) == false);
  CHECK(f.push(false) == false);
}

TEST_CASE("debounce: accepts after threshold consecutive same") {
  auto f{flt::debounce<bool, 3>{}};
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(true));
  static_cast<void>(f.push(true));
  CHECK(f.push(true) == true);  // 3rd consecutive true
}

TEST_CASE("debounce: threshold of 1 accepts a change immediately") {
  auto f{flt::debounce<int, 1>{0}};
  CHECK(f.push(1) == 1);  // one differing sample is enough at Threshold == 1
  CHECK(f.push(2) == 2);
  auto g{flt::debounce<int, 1>{}};
  static_cast<void>(g.push(0));
  CHECK(g.push(1) == 1);
}

TEST_CASE("debounce: threshold-1 samples do not flip early") {
  auto f{flt::debounce<bool, 3>{}};
  static_cast<void>(f.push(false));
  CHECK(f.push(true) == false);  // 1st of a new value
  CHECK(f.push(true) == false);  // 2nd, still one short of 3
  CHECK(f.push(true) == true);   // 3rd flips
}

TEST_CASE("glitch: width of 1 accepts a single sample immediately") {
  auto f{flt::glitch<int, 1>{0}};
  CHECK(f.push(1) == 1);  // a one-sample pulse meets the minimum width of 1
  CHECK(f.push(0) == 0);
}

TEST_CASE("hysteresis: Schmitt trigger behaviour") {
  auto f{flt::hysteresis{20.0, 25.0}};
  CHECK(f.push(10.0) == false);
  CHECK(f.push(22.0) == false);  // in dead band, stays false
  CHECK(f.push(26.0) == true);   // above high
  CHECK(f.push(22.0) == true);   // in dead band, stays true
  CHECK(f.push(19.0) == false);  // below low
}

TEST_CASE("hysteresis: reset clears state") {
  auto f{flt::hysteresis{20.0, 25.0}};
  static_cast<void>(f.push(30.0));
  CHECK(f.value() == true);
  f.reset();
  CHECK(f.value() == false);
}

TEST_CASE("glitch: suppresses a transient pulse shorter than N") {
  auto f{flt::glitch<bool, 3>{}};
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));

  // Transient: one-sample true then back to false.
  CHECK(f.push(true) == false);
  CHECK(f.push(false) == false);  // returned to stable, pending cancelled
}

TEST_CASE("glitch: accepts a persistent state change after N samples") {
  auto f{flt::glitch<bool, 3>{}};
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));
  static_cast<void>(f.push(false));

  CHECK(f.push(true) == false);  // hold 1
  CHECK(f.push(true) == false);  // hold 2
  CHECK(f.push(true) == true);   // hold 3 -> accepted
}

TEST_CASE("glitch: pending() reports transition in progress") {
  auto f{flt::glitch<bool, 3>{false}};
  CHECK_FALSE(f.pending());
  static_cast<void>(f.push(true));
  CHECK(f.pending());
  static_cast<void>(f.push(false));
  CHECK_FALSE(f.pending());  // cancelled
}

TEST_CASE("majority: corrects a single corrupted read out of 3") {
  auto f{flt::majority<int, 3>{}};
  static_cast<void>(f.push(42));
  static_cast<void>(f.push(42));
  CHECK(f.push(99) == 42);  // 2 × 42 vs 1 × 99 -> 42 wins
}

TEST_CASE("majority: tracks a real change when majority shifts") {
  auto f{flt::majority<int, 3>{}};
  static_cast<void>(f.push(10));
  static_cast<void>(f.push(10));
  static_cast<void>(f.push(10));
  static_cast<void>(f.push(20));
  static_cast<void>(f.push(20));
  CHECK(f.push(20) == 20);  // 3 × 20 in window -> 20 wins
}

TEST_CASE("range_guard: rejects out-of-range and holds last valid") {
  auto f{flt::range_guard{0.0, 100.0}};
  CHECK(f.push(50.0) == doctest::Approx(50.0));
  CHECK(f.push(200.0) == doctest::Approx(50.0));  // rejected
  CHECK(f.push(75.0) == doctest::Approx(75.0));   // accepted
  CHECK(f.push(-10.0) == doctest::Approx(75.0));  // rejected
}

TEST_CASE("range_guard: clamps first sample if out of range") {
  auto f{flt::range_guard{10.0, 90.0}};
  CHECK(f.push(200.0) == doctest::Approx(90.0));
}

TEST_CASE("rate_guard: rejects wild jumps") {
  auto f{flt::rate_guard{5.0}};
  CHECK(f.push(100.0) == doctest::Approx(100.0));  // first seeds
  CHECK(f.push(103.0) == doctest::Approx(103.0));  // within delta
  CHECK(f.push(200.0) == doctest::Approx(103.0));  // too far, rejected
  CHECK(f.push(105.0) == doctest::Approx(105.0));  // back within range
}

TEST_CASE("stale_detector: flags after N identical samples") {
  auto f{flt::stale_detector<int, 3>{}};
  static_cast<void>(f.push(42));
  CHECK_FALSE(f.is_stale());
  static_cast<void>(f.push(42));
  CHECK_FALSE(f.is_stale());
  static_cast<void>(f.push(42));
  CHECK(f.is_stale());
  CHECK(f.streak() == 3);
}

TEST_CASE("stale_detector: resets streak on new value") {
  auto f{flt::stale_detector<int, 3>{}};
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(1));
  static_cast<void>(f.push(1));
  CHECK(f.is_stale());
  static_cast<void>(f.push(2));
  CHECK_FALSE(f.is_stale());
  CHECK(f.streak() == 1);
}

TEST_CASE("stale_detector: passes through all values unchanged") {
  auto f{flt::stale_detector<int, 5>{}};
  CHECK(f.push(10) == 10);
  CHECK(f.push(20) == 20);
  CHECK(f.push(30) == 30);
}

TEST_CASE("validator: accepts samples matching the predicate") {
  auto f{flt::validator{[](int x) { return x > 0; }, int{0}}};
  CHECK(f.push(10) == 10);
  CHECK(f.push(20) == 20);
}

TEST_CASE("validator: rejects samples failing the predicate, holds last valid") {
  auto f{flt::validator{[](int x) { return x > 0; }, int{0}}};
  static_cast<void>(f.push(5));
  CHECK(f.push(-1) == 5);  // rejected, holds 5
  CHECK(f.push(0) == 5);   // rejected, holds 5
  CHECK(f.push(7) == 7);   // accepted
}

// ---- FIR, Butterworth, LMS ----

TEST_CASE("fir: matches a hand-computed convolution for a 3-tap kernel") {
  // index 0 weights the newest sample.
  auto const coeffs{std::array<double, 3>{0.5, 0.25, 0.25}};
  auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};

  // push 1: 0.5*1                       = 0.5
  CHECK(f.push(1.0) == doctest::Approx(0.5));
  // push 2: 0.5*2 + 0.25*1              = 1.25
  CHECK(f.push(2.0) == doctest::Approx(1.25));
  // push 3: 0.5*3 + 0.25*2 + 0.25*1     = 2.25
  CHECK(f.push(3.0) == doctest::Approx(2.25));
  // push 4: 0.5*4 + 0.25*3 + 0.25*2     = 3.25
  CHECK(f.push(4.0) == doctest::Approx(3.25));
}

TEST_CASE("fir: impulse response reproduces the kernel in order") {
  auto const coeffs{std::array<double, 3>{2.0, -1.0, 0.5}};
  auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};

  // A unit impulse followed by zeros walks the kernel out tap by tap.
  CHECK(f.push(1.0) == doctest::Approx(2.0));
  CHECK(f.push(0.0) == doctest::Approx(-1.0));
  CHECK(f.push(0.0) == doctest::Approx(0.5));
  CHECK(f.push(0.0) == doctest::Approx(0.0));  // kernel fully shifted out
}

TEST_CASE("fir: default-constructed filter outputs zero") {
  auto f{flt::fir<double, 4>{}};
  CHECK(f.push(123.0) == doctest::Approx(0.0));
  CHECK(f.push(-7.0) == doctest::Approx(0.0));
}

TEST_CASE("fir: coefficients changes the response") {
  auto f{flt::fir<double, 2>{}};
  auto const coeffs{std::array<double, 2>{1.0, 1.0}};  // running sum of 2
  f.coefficients(std::span<double const, 2>{coeffs});
  CHECK(f.push(3.0) == doctest::Approx(3.0));  // 1*3 + 1*0
  CHECK(f.push(4.0) == doctest::Approx(7.0));  // 1*4 + 1*3
  CHECK(f.coefficients()[0] == doctest::Approx(1.0));
  CHECK(f.coefficients()[1] == doctest::Approx(1.0));
}

TEST_CASE("fir: value mirrors the last push; reset clears history") {
  auto const coeffs{std::array<double, 2>{1.0, 1.0}};
  auto f{flt::fir<double, 2>{std::span<double const, 2>{coeffs}}};
  auto const y{f.push(5.0)};
  CHECK(f.value() == doctest::Approx(y));
  CHECK(f.push(6.0) == doctest::Approx(11.0));  // out = 6 + 5 = 11
  CHECK(f.value() == doctest::Approx(11.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  // After reset history is silent, so the next push sees only the new sample.
  CHECK(f.push(2.0) == doctest::Approx(2.0));
}

static_assert(flt::filter_like<flt::fir<double, 4>>);
static_assert(flt::filter_like<flt::butterworth<double, 2>>);

TEST_CASE("butterworth: default-constructed sections pass input unchanged") {
  // biquad default coefficients: b0=1, rest 0 -> y[n] = x[n].
  auto f{flt::butterworth<double, 2>{}};
  CHECK(f.push(3.14) == doctest::Approx(3.14));
}

TEST_CASE("butterworth: low-pass design passes DC") {
  auto f{flt::butterworth<double, 2>{}};
  f.design_low_pass(50.0, 1000.0);
  for (auto i{0}; i < 300; ++i) {
    static_cast<void>(f.push(1.0));
  }
  CHECK(f.value() == doctest::Approx(1.0).epsilon(0.01));
}

TEST_CASE("butterworth: low-pass design attenuates above cutoff") {
  auto f{flt::butterworth<double, 2>{}};
  f.design_low_pass(10.0, 1000.0);
  for (auto i{0}; i < 500; ++i) {
    auto const x{std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(i) / 1000.0)};
    static_cast<void>(f.push(x));
  }
  CHECK(std::abs(f.value()) < 0.01);
}

TEST_CASE("butterworth: high-pass design removes DC") {
  auto f{flt::butterworth<double, 2>{}};
  f.design_high_pass(50.0, 1000.0);
  for (auto i{0}; i < 500; ++i) {
    static_cast<void>(f.push(1.0));
  }
  CHECK(std::abs(f.value()) < 0.01);
}

TEST_CASE("butterworth: reset clears state but preserves coefficients") {
  auto f{flt::butterworth<double, 1>{}};
  f.design_low_pass(100.0, 1000.0);
  for (auto i{0}; i < 100; ++i)
    static_cast<void>(f.push(1.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  // Coefficients preserved: DC should settle again after re-running.
  for (auto i{0}; i < 300; ++i)
    static_cast<void>(f.push(1.0));
  CHECK(f.value() == doctest::Approx(1.0).epsilon(0.01));
}

TEST_CASE("lms: default construction reports zero taps and the default step") {
  auto f{flt::lms<double, 4>{}};
  CHECK(f.step_size() == doctest::Approx(0.01));
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  // With zero taps the first output is zero, error equals the desired.
  auto const y{f.push(1.0, 5.0)};
  CHECK(y == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(5.0));
}

TEST_CASE("lms: identifies a simple gain system") {
  // Desired d[n] = 3 * x[n]. A 1-tap LMS should converge to w[0] -> 3.
  auto f{flt::lms<double, 1>{0.1}};
  auto x{1.0};
  for (auto i{0}; i < 500; ++i) {
    // Alternate a couple of input levels so the filter sees real drive.
    x = (i % 2 == 0) ? 1.0 : 0.5;
    auto const d{3.0 * x};
    static_cast<void>(f.push(x, d));
  }
  CHECK(f.coefficients()[0] == doctest::Approx(3.0).epsilon(0.05));
  CHECK(std::abs(f.error()) < 0.05);  // error has shrunk toward zero
}

TEST_CASE("lms: error decreases as the filter adapts") {
  // 2-tap FIR target: d[n] = 2*x[n] + 1*x[n-1].
  auto f{flt::lms<double, 2>{0.05}};
  auto prev{0.0};
  auto first_abs_err{0.0};
  auto last_abs_err{0.0};
  for (auto i{0}; i < 2000; ++i) {
    // Deterministic pseudo-input via a simple LCG-style recurrence,
    // kept bounded so the update stays stable. No <random> needed.
    auto const x{
      std::sin(0.3 * static_cast<double>(i)) + 0.5 * std::cos(0.11 * static_cast<double>(i))
    };
    auto const d{2.0 * x + 1.0 * prev};
    static_cast<void>(f.push(x, d));
    if (i == 5)
      first_abs_err = std::abs(f.error());
    if (i == 1999)
      last_abs_err = std::abs(f.error());
    prev = x;
  }
  CHECK(last_abs_err < first_abs_err);
  CHECK(f.coefficients()[0] == doctest::Approx(2.0).epsilon(0.1));
  CHECK(f.coefficients()[1] == doctest::Approx(1.0).epsilon(0.1));
}

TEST_CASE("lms: reset clears taps and history but preserves the step size") {
  auto f{flt::lms<double, 2>{0.07}};
  for (auto i{0}; i < 50; ++i)
    static_cast<void>(f.push(1.0, 2.0));
  CHECK(f.coefficients()[0] != doctest::Approx(0.0));  // adapted away from zero
  f.reset();
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(0.0));
  CHECK(f.step_size() == doctest::Approx(0.07));  // step preserved
}

TEST_CASE("lms: step_size updates the adaptation rate") {
  auto f{flt::lms<double, 3>{}};
  CHECK(f.step_size() == doctest::Approx(0.01));
  f.step_size(0.2);
  CHECK(f.step_size() == doctest::Approx(0.2));
}

// ---- timed_debounce ----

using ns = std::chrono::nanoseconds;
using debounce = nexenne::filter::timed_debounce<ns>;
using namespace std::chrono_literals;

TEST_CASE("timed_debounce: first sample is accepted immediately") {
  debounce db{20ms};
  auto const r{db.update(ns{0}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
  CHECK(db.stable_value() == true);
}

TEST_CASE("timed_debounce: same-as-stable samples never produce a result") {
  debounce db{20ms};
  static_cast<void>(db.update(ns{0}, true));
  auto const r{db.update(ns{500'000}, true)};
  CHECK_FALSE(r.has_value());
}

TEST_CASE("timed_debounce: candidate is rejected if it does not hold for the period") {
  debounce db{20ms};
  static_cast<void>(db.update(ns{0}, false));     // stable=false
  auto const r1{db.update(ns{1'000'000}, true)};  // candidate=true at t=1ms
  CHECK_FALSE(r1.has_value());

  auto const r2{db.update(ns{2'000'000}, false)};
  CHECK_FALSE(r2.has_value());
  CHECK(db.stable_value() == false);
}

TEST_CASE("timed_debounce: candidate held for the period is promoted to stable") {
  debounce db{20ms};
  static_cast<void>(db.update(ns{0}, false));
  auto const r1{db.update(ns{1'000'000}, true)};
  CHECK_FALSE(r1.has_value());
  auto const r2{db.update(ns{1'000'000 + 20'000'000}, true)};
  REQUIRE(r2.has_value());
  CHECK(*r2 == true);
  CHECK(db.stable_value() == true);
}

TEST_CASE("timed_debounce: zero period collapses to pass-through on change") {
  debounce db{0ms};
  static_cast<void>(db.update(ns{0}, false));
  auto const r{db.update(ns{1}, true)};
  REQUIRE(r.has_value());
  CHECK(*r == true);
}

TEST_CASE("timed_debounce: reset clears stable state") {
  debounce db{20ms};
  static_cast<void>(db.update(ns{0}, true));
  CHECK(db.has_stable());
  db.reset();
  CHECK_FALSE(db.has_stable());
}

}  // namespace
