#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <limits>

#include <nexenne/math/power.hpp>

namespace math = nexenne::math;

namespace {
constexpr auto rel_close(double const a, double const b, double const tol = 1e-12) -> bool {
  auto const diff{a > b ? a - b : b - a};
  return diff <= (b < 0.0 ? -b : b) * tol;
}
}  // namespace

TEST_CASE("sqrt is constexpr and matches libm at runtime") {
  static_assert(math::sqrt(4.0) == 2.0);
  static_assert(math::sqrt(0.0) == 0.0);
  CHECK(math::sqrt(2.0) == doctest::Approx(std::sqrt(2.0)));
  CHECK(math::sqrt(1e6) == doctest::Approx(1000.0));
  CHECK(math::inv_sqrt(4.0) == doctest::Approx(0.5));
}

TEST_CASE("pow_int via exponentiation by squaring") {
  static_assert(math::pow_int(2.0, 10) == 1024.0);
  static_assert(math::pow_int(2.0, 0) == 1.0);
  static_assert(math::pow_int(5.0, 1) == 5.0);
  static_assert(math::pow_int(2.0, -2) == 0.25);
  CHECK(math::pow_int(3.0, 4) == doctest::Approx(81.0));
}

TEST_CASE("fast_inv_sqrt is within tolerance for float and double") {
  for (double x : {0.5, 1.0, 2.0, 9.0, 100.0, 1234.5}) {
    auto const reference{1.0 / std::sqrt(x)};
    CHECK(math::fast_inv_sqrt(x) == doctest::Approx(reference).epsilon(1e-4));
    CHECK(
      math::fast_inv_sqrt(static_cast<float>(x))
      == doctest::Approx(static_cast<double>(reference)).epsilon(1e-4)
    );
  }
  static_assert(math::fast_inv_sqrt(1.0) > 0.0);
}

TEST_CASE("fast_exp approximates std::exp") {
  for (double x : {-3.0, -1.0, 0.0, 1.0, 2.5, 5.0}) {
    CHECK(math::fast_exp(x) == doctest::Approx(std::exp(x)).epsilon(1e-3));
  }
  CHECK(math::fast_exp(0.0) == doctest::Approx(1.0).epsilon(1e-5));
}

TEST_CASE("fast_log approximates std::log") {
  for (double x : {0.1, 0.5, 1.0, 2.0, 10.0, 1000.0}) {
    CHECK(math::fast_log(x) == doctest::Approx(std::log(x)).epsilon(1e-5));
  }
  CHECK(math::fast_log(1.0) == doctest::Approx(0.0).epsilon(1e-6));
}

static_assert(math::detail::ieee_float<float>);
static_assert(math::detail::ieee_float<double>);
static_assert(!math::detail::ieee_float<long double>);

TEST_CASE("constexpr sqrt converges across the whole exponent range") {
  static_assert(rel_close(math::sqrt(1e20), 1e10));
  static_assert(rel_close(math::sqrt(1e-20), 1e-10));
  static_assert(rel_close(math::sqrt(1e300), 1e150));
  static_assert(rel_close(math::sqrt(1e-300), 1e-150));
  static_assert(math::sqrt(4.0) == 2.0);
  CHECK(math::sqrt(1e200) == doctest::Approx(1e100));
}

TEST_CASE("fast_log handles subnormals") {
  CHECK(math::fast_log(5e-310) == doctest::Approx(std::log(5e-310)).epsilon(1e-6));
  CHECK(math::fast_log(1e-320) == doctest::Approx(std::log(1e-320)).epsilon(1e-6));
}

TEST_CASE("constexpr sqrt does not hang on infinity") {
  // inf * 0.25 == inf, so an unguarded range reduction never terminates.
  constexpr auto inf{std::numeric_limits<double>::infinity()};
  static_assert(math::sqrt(inf) == inf);
}

TEST_CASE("fast_log handles non-positive input instead of diverging") {
  CHECK(math::fast_log(0.0) == -std::numeric_limits<double>::infinity());
  CHECK(math::fast_log(0.0f) == -std::numeric_limits<float>::infinity());
  CHECK(std::isnan(math::fast_log(-1.0)));
  CHECK(std::isnan(math::fast_log(-1.0f)));
  CHECK(std::isnan(math::fast_log(-1e-300)));
  // Tautology on purpose: only the constant evaluation is under test.
  static_assert(math::fast_log(1.0) == 0.0 || math::fast_log(1.0) != 0.0);
}

TEST_CASE("constexpr sqrt is bit-exact and consistent for the boundary") {
  constexpr auto const_sqrt2{math::sqrt(2.0)};
  constexpr auto const_sqrt3{math::sqrt(3.0)};
  CHECK(const_sqrt2 == std::sqrt(2.0));
  CHECK(const_sqrt3 == std::sqrt(3.0));
  // NaN at compile time too: only NaN compares unequal to itself.
  static_assert(math::sqrt(-1.0) != math::sqrt(-1.0));
  CHECK(std::isnan(math::sqrt(-1.0)));
  static_assert(math::sqrt(0.0) == 0.0);
  static_assert(math::sqrt(-0.0) == -0.0);
}

TEST_CASE("fast_exp builds and approximates std::exp for float") {
  for (auto const x : {-3.0f, -0.5f, 0.0f, 0.75f, 4.0f}) {
    auto const expected{std::exp(x)};
    CHECK(std::abs(math::fast_exp(x) - expected) <= 1e-3f * expected);
  }
}

TEST_CASE("constexpr sqrt rounds like the runtime sqrt") {
  // A double-rounding final step lands one ulp off for this input.
  constexpr double x{0x1.452ee3a67f5b9p-355};
  constexpr double at_compile_time{math::sqrt(x)};
  CHECK(at_compile_time == std::sqrt(x));
}

TEST_CASE("pow_int reaches a subnormal result with a negative exponent") {
  // Computing 2^1074 first overflows to inf, and 1 / inf is 0.
  CHECK(math::pow_int(2.0, -1074) == std::numeric_limits<double>::denorm_min());
  CHECK(math::pow_int(2.0, -3) == 0.125);
}
