/**
 * @file
 * @brief Tests for the nexenne::filter concepts header and the umbrella include.
 *
 * Exercises the \c filter_like concept against every filter family the module
 * exposes (positive cases) and against types that violate the surface in
 * various ways (negative cases), then constructs one filter of each family
 * through the umbrella header to confirm it pulls them all in and they link.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <span>
#include <string>

#include <nexenne/filter/filter.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace flt = nexenne::filter;

// Linear smoothers.
static_assert(flt::filter_like<flt::ema<double>>);
static_assert(flt::filter_like<flt::sma<double, 4>>);
static_assert(flt::filter_like<flt::lowpass<double>>);
static_assert(flt::filter_like<flt::highpass<double>>);
static_assert(flt::filter_like<flt::biquad<double>>);
static_assert(flt::filter_like<flt::butterworth<double, 2>>);
static_assert(flt::filter_like<flt::fir<double, 4>>);

// Nonlinear and robust.
static_assert(flt::filter_like<flt::median<double, 3>>);
static_assert(flt::filter_like<flt::kalman<double>>);
// complementary exposes a single-argument push overload alongside its
// two-sensor fusion overload, so it still models filter_like.
static_assert(flt::filter_like<flt::complementary<double>>);

// Control and shaping.
static_assert(flt::filter_like<flt::slew<double>>);
static_assert(flt::filter_like<flt::debounce<bool, 3>>);
static_assert(flt::filter_like<flt::glitch<bool, 3>>);

// Validation and guards.
static_assert(flt::filter_like<flt::range_guard<double>>);
static_assert(flt::filter_like<flt::rate_guard<double>>);
static_assert(flt::filter_like<flt::majority<int, 3>>);
static_assert(flt::filter_like<flt::stale_detector<int, 3>>);

// The concept is generic over the sample type, not pinned to double.
static_assert(flt::filter_like<flt::ema<float>>);
static_assert(flt::filter_like<flt::sma<long double, 8>>);
static_assert(flt::filter_like<flt::median<int, 5>>);

// A plain scalar has none of the surface.
static_assert(!flt::filter_like<int>);
static_assert(!flt::filter_like<double>);
static_assert(!flt::filter_like<void>);

// hysteresis is a converter: it names input_type / output_type rather than
// value_type, so it deliberately does not model filter_like.
static_assert(!flt::filter_like<flt::hysteresis<double>>);

// lms only offers a two-argument push(input, desired); there is no
// single-argument push, so it does not satisfy the requires-expression.
static_assert(!flt::filter_like<flt::lms<double, 4>>);

// A type with the methods but no value_type member type.
struct no_value_type {
  auto push(double s) -> double {
    return s;
  }

  auto value() const -> double {
    return 0.0;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<no_value_type>);

// A type missing value().
struct no_value_method {
  using value_type = double;

  auto push(value_type s) -> value_type {
    return s;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<no_value_method>);

// A type missing push().
struct no_push_method {
  using value_type = double;

  auto value() const -> value_type {
    return 0.0;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<no_push_method>);

// A type missing reset().
struct no_reset_method {
  using value_type = double;

  auto push(value_type s) -> value_type {
    return s;
  }

  auto value() const -> value_type {
    return 0.0;
  }
};

static_assert(!flt::filter_like<no_reset_method>);

// Wrong push return type: the concept pins push to return value_type exactly.
struct wrong_push_return {
  using value_type = double;

  auto push(value_type) -> int {
    return 0;
  }

  auto value() const -> value_type {
    return 0.0;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<wrong_push_return>);

// Wrong value() return type.
struct wrong_value_return {
  using value_type = double;

  auto push(value_type s) -> value_type {
    return s;
  }

  auto value() const -> int {
    return 0;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<wrong_value_return>);

// push that does not accept the sample type (no callable single-arg overload
// taking value_type), even though every other part of the surface is present.
struct push_takes_no_args {
  using value_type = double;

  auto push() -> value_type {
    return 0.0;
  }

  auto value() const -> value_type {
    return 0.0;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<push_takes_no_args>);

// A library-ish non-filter type.
static_assert(!flt::filter_like<std::string>);

TEST_CASE("nexenne::filter umbrella exposes every family and they run") {
  // Linear smoothers.
  {
    auto f{flt::ema{0.5}};
    nexenne::utility::discard(f.push(1.0));
    CHECK(f.value() == doctest::Approx(1.0));
    f.reset();
  }
  {
    auto f{flt::sma<double, 4>{}};
    nexenne::utility::discard(f.push(2.0));
    CHECK(f.value() == doctest::Approx(2.0));
    f.reset();
  }
  {
    auto f{flt::lowpass{10.0, 1000.0}};
    nexenne::utility::discard(f.push(3.0));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto f{flt::highpass{10.0, 1000.0}};
    nexenne::utility::discard(f.push(3.0));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto f{flt::biquad<double>::make_lowpass(50.0, 1000.0)};
    nexenne::utility::discard(f.push(1.0));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto f{flt::butterworth<double, 2>{}};
    nexenne::utility::discard(f.push(1.0));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto const coeffs{std::array<double, 3>{0.5, 0.25, 0.25}};
    auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};
    nexenne::utility::discard(f.push(1.0));
    nexenne::utility::discard(f.value());
    f.reset();
  }

  // Nonlinear and robust.
  {
    auto f{flt::median<double, 3>{}};
    nexenne::utility::discard(f.push(5.0));
    CHECK(f.value() == doctest::Approx(5.0));
    f.reset();
  }
  {
    auto f{flt::kalman{0.01, 0.1}};
    nexenne::utility::discard(f.push(42.0));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto f{flt::complementary{0.98}};
    nexenne::utility::discard(f.push(10.0, 9.5));  // two-sensor fusion overload
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto f{flt::lms<double, 4>{}};
    nexenne::utility::discard(f.push(1.0, 5.0));  // input, desired
    nexenne::utility::discard(f.value());
    f.reset();
  }

  // Control and shaping.
  {
    auto f{flt::slew{5.0}};
    nexenne::utility::discard(f.push(0.0));
    CHECK(f.push(100.0) == doctest::Approx(5.0));
    f.reset();
  }
  {
    auto f{flt::debounce<bool, 3>{}};
    nexenne::utility::discard(f.push(false));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto f{flt::hysteresis{20.0, 25.0}};
    CHECK(f.push(30.0) == true);
    CHECK(f.value() == true);
    f.reset();
    CHECK(f.value() == false);
  }
  {
    auto f{flt::glitch<bool, 3>{}};
    nexenne::utility::discard(f.push(false));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    using ns = std::chrono::nanoseconds;
    using namespace std::chrono_literals;
    auto db{flt::timed_debounce<ns>{20ms}};
    auto const r{db.update(ns{0}, true)};
    REQUIRE(r.has_value());
    CHECK(*r == true);
    db.reset();
  }

  // Validation and guards.
  {
    auto f{flt::range_guard{0.0, 100.0}};
    CHECK(f.push(50.0) == doctest::Approx(50.0));
    f.reset();
  }
  {
    auto f{flt::rate_guard{5.0}};
    CHECK(f.push(100.0) == doctest::Approx(100.0));
    f.reset();
  }
  {
    auto f{flt::validator{[](int x) { return x > 0; }, int{0}}};
    CHECK(f.push(10) == 10);
    f.reset();
  }
  {
    auto f{flt::majority<int, 3>{}};
    nexenne::utility::discard(f.push(42));
    nexenne::utility::discard(f.value());
    f.reset();
  }
  {
    auto f{flt::stale_detector<int, 3>{}};
    nexenne::utility::discard(f.push(7));
    CHECK(f.value() == 7);
    f.reset();
  }
}

}  // namespace
