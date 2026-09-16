/**
 * @file
 * @brief Deep tests for the robust and adaptive filters
 * (median, kalman, complementary, lms).
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <nexenne/filter/filter.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

static_assert(flt::filter_like<flt::median<double, 3>>);
static_assert(flt::filter_like<flt::median<float, 3>>);
static_assert(flt::filter_like<flt::median<double, 4>>);
static_assert(flt::filter_like<flt::kalman<double>>);
static_assert(flt::filter_like<flt::kalman<float>>);
static_assert(flt::filter_like<flt::complementary<double>>);
static_assert(flt::filter_like<flt::complementary<float>>);
static_assert(!flt::filter_like<flt::lms<double, 4>>);
static_assert(!flt::filter_like<flt::lms<float, 4>>);

static_assert(flt::median<double, 5>::window_size == 5);
static_assert(flt::median<double, 3>::window_size == 3);
static_assert(flt::lms<double, 7>::taps == 7);

TEST_CASE("nexenne::filter::median default-constructed state") {
  auto f{flt::median<double, 3>{}};
  CHECK_FALSE(f.filled());
  CHECK(f.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::median rejects a single spike while passing the level") {
  auto f{flt::median<double, 3>{}};
  nexenne::utility::ignore(f.push(10.0));
  nexenne::utility::ignore(f.push(10.0));
  nexenne::utility::ignore(f.push(10.0));
  CHECK(f.filled());
  CHECK(f.push(1000.0) == doctest::Approx(10.0));
  CHECK(f.push(10.0) == doctest::Approx(10.0));
  CHECK(f.value() == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::median window-fill transient (N=4, exact values)") {
  auto f{flt::median<double, 4>{}};
  CHECK_FALSE(f.filled());
  CHECK(f.push(5.0) == doctest::Approx(5.0));
  CHECK_FALSE(f.filled());
  CHECK(f.push(1.0) == doctest::Approx(1.0));
  CHECK(f.push(9.0) == doctest::Approx(5.0));
  CHECK(f.push(3.0) == doctest::Approx(3.0));
  CHECK(f.filled());
}

TEST_CASE("nexenne::filter::median EVEN window returns the LOWER-middle element") {
  auto f{flt::median<double, 4>{}};
  nexenne::utility::ignore(f.push(4.0));
  nexenne::utility::ignore(f.push(2.0));
  nexenne::utility::ignore(f.push(1.0));
  auto const v{f.push(3.0)};
  CHECK(v == doctest::Approx(2.0));
  CHECK(f.value() == doctest::Approx(2.0));

  auto g{flt::median<double, 4>{}};
  nexenne::utility::ignore(g.push(8.0));
  nexenne::utility::ignore(g.push(6.0));
  nexenne::utility::ignore(g.push(2.0));
  CHECK(g.push(4.0) == doctest::Approx(4.0));
}

TEST_CASE("nexenne::filter::median even window N=2 returns the lower of two") {
  auto f{flt::median<double, 2>{}};
  CHECK(f.push(7.0) == doctest::Approx(7.0));
  CHECK(f.push(3.0) == doctest::Approx(3.0));
  CHECK(f.push(10.0) == doctest::Approx(3.0));
}

TEST_CASE("nexenne::filter::median window size 1 is passthrough") {
  auto f{flt::median<double, 1>{}};
  CHECK(f.push(3.0) == doctest::Approx(3.0));
  CHECK(f.filled());
  CHECK(f.push(-100.0) == doctest::Approx(-100.0));
  CHECK(f.push(42.5) == doctest::Approx(42.5));
  CHECK(f.value() == doctest::Approx(42.5));
}

TEST_CASE("nexenne::filter::median monotonic ramp through a full window") {
  auto f{flt::median<double, 3>{}};
  nexenne::utility::ignore(f.push(1.0));
  nexenne::utility::ignore(f.push(2.0));
  CHECK(f.push(3.0) == doctest::Approx(2.0));
  CHECK(f.push(4.0) == doctest::Approx(3.0));
  CHECK(f.push(5.0) == doctest::Approx(4.0));
  CHECK(f.push(6.0) == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::median tracks a step after a majority of the window flips") {
  auto f{flt::median<double, 5>{}};
  for (auto i{0}; i < 5; ++i) {
    nexenne::utility::ignore(f.push(0.0));
  }
  CHECK(f.value() == doctest::Approx(0.0));
  nexenne::utility::ignore(f.push(100.0));
  CHECK(f.value() == doctest::Approx(0.0));
  nexenne::utility::ignore(f.push(100.0));
  CHECK(f.value() == doctest::Approx(0.0));
  auto const v{f.push(100.0)};
  CHECK(v == doctest::Approx(100.0));
}

TEST_CASE("nexenne::filter::median repeated (all-equal) values") {
  auto f{flt::median<double, 5>{}};
  for (auto i{0}; i < 20; ++i) {
    CHECK(f.push(7.0) == doctest::Approx(7.0));
  }
  CHECK(f.value() == doctest::Approx(7.0));
}

TEST_CASE("nexenne::filter::median alternating extremes settle on a stable median") {
  auto f{flt::median<double, 3>{}};
  nexenne::utility::ignore(f.push(-1e6));
  nexenne::utility::ignore(f.push(1e6));
  CHECK(f.push(-1e6) == doctest::Approx(-1e6));
  CHECK(f.push(1e6) == doctest::Approx(1e6));
}

TEST_CASE("nexenne::filter::median single non-zero sample among zeros") {
  auto f{flt::median<double, 5>{}};
  for (auto i{0}; i < 5; ++i) {
    nexenne::utility::ignore(f.push(0.0));
  }
  CHECK(f.push(999.0) == doctest::Approx(0.0));
  CHECK(f.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::median reset clears the window and value") {
  auto f{flt::median<double, 3>{}};
  nexenne::utility::ignore(f.push(5.0));
  nexenne::utility::ignore(f.push(6.0));
  nexenne::utility::ignore(f.push(7.0));
  CHECK(f.filled());
  CHECK(f.value() == doctest::Approx(6.0));
  f.reset();
  CHECK_FALSE(f.filled());
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.push(42.0) == doctest::Approx(42.0));
  CHECK_FALSE(f.filled());
}

TEST_CASE("nexenne::filter::median very long run rejects intermittent spikes") {
  auto f{flt::median<double, 5>{}};
  auto last{0.0};
  for (auto i{0}; i < 1000; ++i) {
    auto const base{50.0};
    auto const x{(i % 7 == 0) ? 1.0e9 : base};
    last = f.push(x);
    if (i > 10) {
      CHECK(last == doctest::Approx(base));
    }
  }
  CHECK(last == doctest::Approx(50.0));
}

TEST_CASE("nexenne::filter::median float instantiation matches double behaviour") {
  auto f{flt::median<float, 3>{}};
  nexenne::utility::ignore(f.push(10.0F));
  nexenne::utility::ignore(f.push(10.0F));
  nexenne::utility::ignore(f.push(10.0F));
  CHECK(f.push(1000.0F) == doctest::Approx(10.0));
  CHECK(f.value() == doctest::Approx(10.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::kalman first sample seeds the estimate (no transient)") {
  auto kf{flt::kalman{0.1, 0.5}};
  CHECK(kf.push(42.0) == doctest::Approx(42.0));
  CHECK(kf.value() == doctest::Approx(42.0));
}

TEST_CASE("nexenne::filter::kalman converges to a constant measurement") {
  auto kf{flt::kalman{0.01, 0.1}};
  for (auto i{0}; i < 50; ++i) {
    nexenne::utility::ignore(kf.push(42.0));
  }
  CHECK(kf.value() == doctest::Approx(42.0).epsilon(0.01));
}

TEST_CASE("nexenne::filter::kalman noisy constant converges to the true level") {
  auto kf{flt::kalman{0.001, 1.0}};
  for (auto i{0}; i < 400; ++i) {
    auto const dither{(i % 2 == 0) ? 1.0 : -1.0};
    nexenne::utility::ignore(kf.push(20.0 + dither));
  }
  CHECK(kf.value() == doctest::Approx(20.0).epsilon(0.05));
}

TEST_CASE("nexenne::filter::kalman tracks a step change") {
  auto kf{flt::kalman{0.1, 0.5}};
  for (auto i{0}; i < 20; ++i) {
    nexenne::utility::ignore(kf.push(0.0));
  }
  for (auto i{0}; i < 50; ++i) {
    nexenne::utility::ignore(kf.push(100.0));
  }
  CHECK(kf.value() == doctest::Approx(100.0).epsilon(1.0));
}

TEST_CASE("nexenne::filter::kalman exact one-step update (Q=0, R=1, P0=1)") {
  // Welch and Bishop step: P_pred = 1, K = P_pred / (P_pred + R) = 0.5, x = 2, P = 0.5.
  auto kf{flt::kalman{0.0, 1.0, 0.0, 1.0}};
  CHECK(kf.push(0.0) == doctest::Approx(0.0));
  CHECK(kf.covariance() == doctest::Approx(1.0));
  auto const x{kf.push(4.0)};
  CHECK(x == doctest::Approx(2.0));
  CHECK(kf.covariance() == doctest::Approx(0.5));
}

TEST_CASE("nexenne::filter::kalman gain evolves as covariance shrinks") {
  auto kf{flt::kalman{0.0, 1.0, 0.0, 1.0}};
  CHECK(kf.gain() == doctest::Approx(0.5));
  nexenne::utility::ignore(kf.push(10.0));
  CHECK(kf.gain() == doctest::Approx(0.5));
  nexenne::utility::ignore(kf.push(10.0));
  CHECK(kf.covariance() == doctest::Approx(0.5));
  CHECK(kf.gain() == doctest::Approx(1.0 / 3.0));
  nexenne::utility::ignore(kf.push(10.0));
  CHECK(kf.covariance() == doctest::Approx(1.0 / 3.0));
}

TEST_CASE("nexenne::filter::kalman zero-variance / zero-denominator stays finite (NO NaN)") {
  auto kf{flt::kalman{0.0, 0.0, 0.0, 0.0}};
  CHECK(kf.gain() == doctest::Approx(0.0));
  auto const a{kf.push(5.0)};
  CHECK(a == doctest::Approx(5.0));
  CHECK(std::isfinite(a));
  auto const b{kf.push(7.0)};
  CHECK(std::isfinite(b));
  CHECK(b == doctest::Approx(5.0));
  CHECK(std::isfinite(kf.value()));
  CHECK(std::isfinite(kf.covariance()));
  CHECK(kf.gain() == doctest::Approx(0.0));
  for (auto i{0}; i < 100; ++i) {
    auto const v{kf.push(static_cast<double>(i))};
    CHECK(std::isfinite(v));
    CHECK(v == doctest::Approx(5.0));
  }
}

TEST_CASE("nexenne::filter::kalman process vs measurement noise change convergence speed") {
  auto agile{flt::kalman{1.0, 0.1}};
  auto sluggish{flt::kalman{0.001, 100.0}};
  nexenne::utility::ignore(agile.push(0.0));
  nexenne::utility::ignore(sluggish.push(0.0));
  for (auto i{0}; i < 10; ++i) {
    nexenne::utility::ignore(agile.push(100.0));
    nexenne::utility::ignore(sluggish.push(100.0));
  }
  CHECK(agile.value() > sluggish.value());
  CHECK(agile.value() == doctest::Approx(100.0).epsilon(0.05));
  CHECK(sluggish.value() == doctest::Approx(1000.0 / 11.0).epsilon(0.01));
}

TEST_CASE("nexenne::filter::kalman noise() swaps parameters without touching state") {
  auto kf{flt::kalman{0.0, 1.0, 0.0, 1.0}};
  nexenne::utility::ignore(kf.push(10.0));
  CHECK(kf.gain() == doctest::Approx(0.5));
  kf.noise(0.0, 3.0);
  CHECK(kf.value() == doctest::Approx(10.0));
  CHECK(kf.covariance() == doctest::Approx(1.0));
  CHECK(kf.gain() == doctest::Approx(0.25));
}

TEST_CASE("nexenne::filter::kalman reset reseeds estimate, covariance, and priming") {
  auto kf{flt::kalman{0.1, 0.5}};
  for (auto i{0}; i < 30; ++i) {
    nexenne::utility::ignore(kf.push(50.0));
  }
  CHECK(kf.value() == doctest::Approx(50.0).epsilon(0.1));
  kf.reset(5.0, 2.0);
  CHECK(kf.value() == doctest::Approx(5.0));
  CHECK(kf.covariance() == doctest::Approx(2.0));
  CHECK(kf.push(99.0) == doctest::Approx(99.0));

  kf.reset();
  CHECK(kf.value() == doctest::Approx(0.0));
  CHECK(kf.covariance() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::filter::kalman all-equal input keeps the estimate pinned") {
  auto kf{flt::kalman{0.01, 0.1}};
  nexenne::utility::ignore(kf.push(7.0));
  for (auto i{0}; i < 200; ++i) {
    auto const v{kf.push(7.0)};
    CHECK(v == doctest::Approx(7.0));
  }
}

TEST_CASE("nexenne::filter::kalman float instantiation seeds and converges") {
  auto kf{flt::kalman<float>{0.01F, 0.1F}};
  CHECK(kf.push(3.0F) == doctest::Approx(3.0));
  for (auto i{0}; i < 50; ++i) {
    nexenne::utility::ignore(kf.push(3.0F));
  }
  CHECK(kf.value() == doctest::Approx(3.0).epsilon(0.01));
  CHECK(std::isfinite(kf.value()));
}

TEST_CASE("nexenne::filter::complementary blends two sensors by alpha") {
  auto cf{flt::complementary{0.98}};
  CHECK(cf.alpha() == doctest::Approx(0.98));
  auto const y{cf.push(10.0, 9.5)};
  CHECK(y == doctest::Approx(9.99));
  CHECK(cf.value() == doctest::Approx(9.99));
}

TEST_CASE("nexenne::filter::complementary alpha=1 selects the fast source exactly") {
  auto cf{flt::complementary{1.0}};
  CHECK(cf.push(12.0, -99.0) == doctest::Approx(12.0));
  CHECK(cf.push(3.5, 1000.0) == doctest::Approx(3.5));
}

TEST_CASE("nexenne::filter::complementary alpha=0 selects the slow source exactly") {
  auto cf{flt::complementary{0.0}};
  CHECK(cf.push(-99.0, 12.0) == doctest::Approx(12.0));
  CHECK(cf.push(1000.0, 3.5) == doctest::Approx(3.5));
}

TEST_CASE("nexenne::filter::complementary intermediate alpha is the exact weighted sum") {
  auto cf{flt::complementary{0.25}};
  CHECK(cf.push(8.0, 4.0) == doctest::Approx(5.0));
  CHECK(cf.push(-4.0, 8.0) == doctest::Approx(5.0));
  CHECK(cf.push(100.0, 0.0) == doctest::Approx(25.0));
}

TEST_CASE("nexenne::filter::complementary single-input overload is a first-order low-pass") {
  auto cf{flt::complementary{0.5}};
  CHECK(cf.value() == doctest::Approx(0.0));
  CHECK(cf.push(10.0) == doctest::Approx(5.0));
  CHECK(cf.push(10.0) == doctest::Approx(7.5));
  CHECK(cf.push(10.0) == doctest::Approx(8.75));
  for (auto i{0}; i < 100; ++i) {
    nexenne::utility::ignore(cf.push(10.0));
  }
  CHECK(cf.value() == doctest::Approx(10.0).epsilon(0.001));
}

TEST_CASE("nexenne::filter::complementary high/low-pass split sums to the inputs at extremes") {
  auto cf{flt::complementary{0.7}};
  CHECK(cf.push(5.0, 5.0) == doctest::Approx(5.0));
  cf.alpha() = 0.3;
  CHECK(cf.push(-2.0, -2.0) == doctest::Approx(-2.0));
}

TEST_CASE("nexenne::filter::complementary alpha() setter changes subsequent blends") {
  auto cf{flt::complementary{0.5}};
  CHECK(cf.push(10.0, 0.0) == doctest::Approx(5.0));
  cf.alpha() = 0.9;
  CHECK(cf.alpha() == doctest::Approx(0.9));
  CHECK(cf.push(10.0, 0.0) == doctest::Approx(9.0));
}

TEST_CASE("nexenne::filter::complementary reset variants") {
  auto cf{flt::complementary{0.5}};
  nexenne::utility::ignore(cf.push(10.0, 10.0));
  CHECK(cf.value() == doctest::Approx(10.0));
  cf.reset();
  CHECK(cf.value() == doctest::Approx(0.0));
  cf.reset(3.0);
  CHECK(cf.value() == doctest::Approx(3.0));
  CHECK(cf.push(3.0) == doctest::Approx(3.0));
  CHECK(cf.alpha() == doctest::Approx(0.5));
}

TEST_CASE("nexenne::filter::complementary alternating extremes blend deterministically") {
  auto cf{flt::complementary{0.5}};
  CHECK(cf.push(1e6, -1e6) == doctest::Approx(0.0));
  CHECK(cf.push(-1e6, 1e6) == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::complementary float instantiation") {
  auto cf{flt::complementary<float>{0.25F}};
  CHECK(cf.push(8.0F, 4.0F) == doctest::Approx(5.0));
  cf.reset();
  CHECK(cf.value() == doctest::Approx(0.0));
  CHECK(cf.push(4.0F, 8.0F) == doctest::Approx(7.0));
}

TEST_CASE("nexenne::filter::lms default construction reports zero taps and the default step") {
  auto f{flt::lms<double, 4>{}};
  CHECK(f.step_size() == doctest::Approx(0.01));
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(0.0));
  auto const y{f.push(1.0, 5.0)};
  CHECK(y == doctest::Approx(0.0));
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::lms hand-computed two-step update (N=2, mu=0.1)") {
  // LMS step 1: y = 0, e = 5, w0 += 0.1 * 5 * 2 = 1, w1 += 0.1 * 5 * 0 = 0.
  // LMS step 2: y = 1 * 3 = 3, e = 4, w0 = 1 + 0.1 * 4 * 3 = 2.2, w1 = 0.1 * 4 * 2 = 0.8.
  auto f{flt::lms<double, 2>{0.1}};
  auto const y0{f.push(2.0, 5.0)};
  CHECK(y0 == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(5.0));
  CHECK(f.coefficients()[0] == doctest::Approx(1.0));
  CHECK(f.coefficients()[1] == doctest::Approx(0.0));

  auto const y1{f.push(3.0, 7.0)};
  CHECK(y1 == doctest::Approx(3.0));
  CHECK(f.value() == doctest::Approx(3.0));
  CHECK(f.error() == doctest::Approx(4.0));
  CHECK(f.coefficients()[0] == doctest::Approx(2.2));
  CHECK(f.coefficients()[1] == doctest::Approx(0.8));
}

TEST_CASE("nexenne::filter::lms identifies a simple gain system (stable mu)") {
  // LMS stability bound: 0 < mu < 2 / input power.
  auto f{flt::lms<double, 1>{0.1}};
  auto x{1.0};
  for (auto i{0}; i < 500; ++i) {
    x = (i % 2 == 0) ? 1.0 : 0.5;
    auto const d{3.0 * x};
    nexenne::utility::ignore(f.push(x, d));
  }
  CHECK(f.coefficients()[0] == doctest::Approx(3.0).epsilon(0.05));
  CHECK(std::abs(f.error()) < 0.05);
}

TEST_CASE("nexenne::filter::lms error shrinks over iterations as it adapts") {
  auto f{flt::lms<double, 2>{0.05}};
  auto prev{0.0};
  auto first_abs_err{0.0};
  auto last_abs_err{0.0};
  for (auto i{0}; i < 2000; ++i) {
    auto const x{
      std::sin(0.3 * static_cast<double>(i)) + 0.5 * std::cos(0.11 * static_cast<double>(i))
    };
    auto const d{2.0 * x + 1.0 * prev};
    nexenne::utility::ignore(f.push(x, d));
    if (i == 5) {
      first_abs_err = std::abs(f.error());
    }
    if (i == 1999) {
      last_abs_err = std::abs(f.error());
    }
    prev = x;
  }
  CHECK(last_abs_err < first_abs_err);
  CHECK(f.coefficients()[0] == doctest::Approx(2.0).epsilon(0.1));
  CHECK(f.coefficients()[1] == doctest::Approx(1.0).epsilon(0.1));
}

TEST_CASE("nexenne::filter::lms a stable mu drives the running error toward zero") {
  auto f{flt::lms<double, 1>{0.2}};
  auto const target{4.0};
  auto early_err{0.0};
  auto late_err{0.0};
  for (auto i{0}; i < 200; ++i) {
    nexenne::utility::ignore(f.push(1.0, target));
    if (i == 1) {
      early_err = std::abs(f.error());
    }
    if (i == 199) {
      late_err = std::abs(f.error());
    }
  }
  CHECK(late_err < early_err);
  CHECK(f.coefficients()[0] == doctest::Approx(target).epsilon(0.01));
  CHECK(std::abs(f.error()) < 0.01);
}

TEST_CASE("nexenne::filter::lms step_size getter/setter") {
  auto f{flt::lms<double, 3>{}};
  CHECK(f.step_size() == doctest::Approx(0.01));
  f.step_size() = 0.2;
  CHECK(f.step_size() == doctest::Approx(0.2));
  auto g{flt::lms<double, 3>{0.05}};
  CHECK(g.step_size() == doctest::Approx(0.05));
}

TEST_CASE("nexenne::filter::lms coefficients() exposes N taps") {
  auto f{flt::lms<double, 5>{}};
  CHECK(f.coefficients().size() == std::size_t{5});
  static_assert(decltype(f.coefficients())::extent == std::size_t{5});
}

TEST_CASE("nexenne::filter::lms reset clears taps and history but preserves the step size") {
  auto f{flt::lms<double, 2>{0.07}};
  for (auto i{0}; i < 50; ++i) {
    nexenne::utility::ignore(f.push(1.0, 2.0));
  }
  CHECK(f.coefficients()[0] != doctest::Approx(0.0));
  f.reset();
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(0.0));
  CHECK(f.step_size() == doctest::Approx(0.07));
  CHECK(f.push(2.0, 6.0) == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(6.0));
}

TEST_CASE("nexenne::filter::lms all-zero desired keeps taps at zero from a zero start") {
  auto f{flt::lms<double, 3>{0.1}};
  for (auto i{0}; i < 100; ++i) {
    nexenne::utility::ignore(f.push(1.0, 0.0));
  }
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  CHECK(f.error() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::lms very long stable run stays finite and converged") {
  auto f{flt::lms<double, 4>{0.05}};
  auto prev{std::array<double, 3>{0.0, 0.0, 0.0}};
  for (auto i{0}; i < 20000; ++i) {
    auto const x{std::sin(0.2 * static_cast<double>(i))};
    auto const d{1.5 * x + 0.5 * prev[0] - 0.25 * prev[1]};
    nexenne::utility::ignore(f.push(x, d));
    prev[2] = prev[1];
    prev[1] = prev[0];
    prev[0] = x;
  }
  for (auto const c : f.coefficients()) {
    CHECK(std::isfinite(c));
  }
  CHECK(std::isfinite(f.value()));
  CHECK(std::abs(f.error()) < 0.05);
}

TEST_CASE("nexenne::filter::lms float instantiation hand-check") {
  auto f{flt::lms<float, 2>{0.1F}};
  CHECK(f.push(2.0F, 5.0F) == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(5.0));
  CHECK(f.coefficients()[0] == doctest::Approx(1.0));
  CHECK(f.push(3.0F, 7.0F) == doctest::Approx(3.0));
  CHECK(f.coefficients()[0] == doctest::Approx(2.2));
  CHECK(f.coefficients()[1] == doctest::Approx(0.8));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.step_size() == doctest::Approx(0.1));
}

TEST_CASE("nexenne::filter::kalman seeds its covariance with the measurement noise") {
  auto kf{flt::kalman{1e-3, 100.0}};
  nexenne::utility::ignore(kf.push(30.0));
  CHECK(kf.covariance() == doctest::Approx(100.0));
  auto estimate{0.0};
  for (auto i{0}; i < 50; ++i) {
    estimate = kf.push(0.0);
  }
  CHECK(estimate < 1.0);
}

TEST_CASE("nexenne::filter::median ignores NaN samples whatever their position") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  std::array<std::array<double, 3>, 3> const orders{
    {{1.0, nan, 100.0}, {nan, 1.0, 100.0}, {100.0, 1.0, nan}}
  };
  for (auto const& order : orders) {
    flt::median<double, 3> m;
    auto out{0.0};
    for (auto const x : order) {
      out = m.push(x);
    }
    CHECK(out == doctest::Approx(1.0));
  }
  flt::median<double, 3> only_nan;
  CHECK(std::isnan(only_nan.push(nan)));
  flt::median<double, 5> spike;
  for (auto const x : {3.0, 4.0, nan, 5.0, 6.0}) {
    nexenne::utility::ignore(spike.push(x));
  }
  CHECK(spike.value() == doctest::Approx(4.0));
}

static_assert(!noexcept(std::declval<flt::median<std::string, 3>&>().push(std::string{})));
static_assert(noexcept(std::declval<flt::median<double, 3>&>().push(1.0)));

}  // namespace
