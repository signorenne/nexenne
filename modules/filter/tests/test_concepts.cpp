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
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

static_assert(flt::filter_like<flt::ema<double>>);
static_assert(flt::filter_like<flt::sma<double, 4>>);
static_assert(flt::filter_like<flt::lowpass<double>>);
static_assert(flt::filter_like<flt::highpass<double>>);
static_assert(flt::filter_like<flt::biquad<double>>);
static_assert(flt::filter_like<flt::butterworth<double, 2>>);
static_assert(flt::filter_like<flt::fir<double, 4>>);

static_assert(flt::filter_like<flt::median<double, 3>>);
static_assert(flt::filter_like<flt::kalman<double>>);
static_assert(flt::filter_like<flt::complementary<double>>);

static_assert(flt::filter_like<flt::slew<double>>);
static_assert(flt::filter_like<flt::debounce<bool, 3>>);
static_assert(flt::filter_like<flt::glitch<bool, 3>>);

static_assert(flt::filter_like<flt::range_guard<double>>);
static_assert(flt::filter_like<flt::rate_guard<double>>);
static_assert(flt::filter_like<flt::majority<int, 3>>);
static_assert(flt::filter_like<flt::stale_detector<int, 3>>);

static_assert(flt::filter_like<flt::ema<float>>);
static_assert(flt::filter_like<flt::sma<long double, 8>>);
static_assert(flt::filter_like<flt::median<int, 5>>);

static_assert(!flt::filter_like<int>);
static_assert(!flt::filter_like<double>);
static_assert(!flt::filter_like<void>);

static_assert(!flt::filter_like<flt::hysteresis<double>>);

static_assert(!flt::filter_like<flt::lms<double, 4>>);

static_assert(!flt::filter_like<flt::timed_debounce<std::chrono::nanoseconds>>);

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

struct no_value_method {
  using value_type = double;

  auto push(value_type s) -> value_type {
    return s;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<no_value_method>);

struct no_push_method {
  using value_type = double;

  auto value() const -> value_type {
    return 0.0;
  }

  auto reset() -> void {}
};

static_assert(!flt::filter_like<no_push_method>);

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

static_assert(!flt::filter_like<std::string>);

TEST_CASE("nexenne::filter umbrella exposes every family and they run") {
  {
    auto f{flt::ema{0.5}};
    nexenne::utility::ignore(f.push(1.0));
    CHECK(f.value() == doctest::Approx(1.0));
    f.reset();
  }
  {
    auto f{flt::sma<double, 4>{}};
    nexenne::utility::ignore(f.push(2.0));
    CHECK(f.value() == doctest::Approx(2.0));
    f.reset();
  }
  {
    auto f{flt::lowpass{10.0, 1000.0}};
    nexenne::utility::ignore(f.push(3.0));
    nexenne::utility::ignore(f.value());
    f.reset();
  }
  {
    auto f{flt::highpass{10.0, 1000.0}};
    nexenne::utility::ignore(f.push(3.0));
    nexenne::utility::ignore(f.value());
    f.reset();
  }
  {
    auto f{flt::biquad<double>::make_lowpass(50.0, 1000.0)};
    nexenne::utility::ignore(f.push(1.0));
    nexenne::utility::ignore(f.value());
    f.reset();
  }
  {
    auto f{flt::butterworth<double, 2>{}};
    nexenne::utility::ignore(f.push(1.0));
    nexenne::utility::ignore(f.value());
    f.reset();
  }
  {
    auto const coeffs{std::array<double, 3>{0.5, 0.25, 0.25}};
    auto f{flt::fir<double, 3>{std::span<double const, 3>{coeffs}}};
    nexenne::utility::ignore(f.push(1.0));
    nexenne::utility::ignore(f.value());
    f.reset();
  }

  {
    auto f{flt::median<double, 3>{}};
    nexenne::utility::ignore(f.push(5.0));
    CHECK(f.value() == doctest::Approx(5.0));
    f.reset();
  }
  {
    auto f{flt::kalman{0.01, 0.1}};
    nexenne::utility::ignore(f.push(42.0));
    nexenne::utility::ignore(f.value());
    f.reset();
  }
  {
    auto f{flt::complementary{0.98}};
    nexenne::utility::ignore(f.push(10.0, 9.5));
    nexenne::utility::ignore(f.value());
    f.reset();
  }
  {
    auto f{flt::lms<double, 4>{}};
    nexenne::utility::ignore(f.push(1.0, 5.0));
    nexenne::utility::ignore(f.value());
    f.reset();
  }

  {
    auto f{flt::slew{5.0}};
    nexenne::utility::ignore(f.push(0.0));
    CHECK(f.push(100.0) == doctest::Approx(5.0));
    f.reset();
  }
  {
    auto f{flt::debounce<bool, 3>{}};
    nexenne::utility::ignore(f.push(false));
    nexenne::utility::ignore(f.value());
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
    nexenne::utility::ignore(f.push(false));
    nexenne::utility::ignore(f.value());
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
    nexenne::utility::ignore(f.push(42));
    nexenne::utility::ignore(f.value());
    f.reset();
  }
  {
    auto f{flt::stale_detector<int, 3>{}};
    nexenne::utility::ignore(f.push(7));
    CHECK(f.value() == 7);
    f.reset();
  }
}

template <typename T>
concept fir_type = requires { typename flt::fir<T, 2>; };
static_assert(!fir_type<std::string>);
static_assert(fir_type<int>);
template <typename D>
concept debounce_duration = requires { typename flt::timed_debounce<D>; };
static_assert(!debounce_duration<int>);
static_assert(debounce_duration<std::chrono::milliseconds>);

}  // namespace
