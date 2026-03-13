#include <doctest/doctest.h>

#include <initializer_list>
#include <type_traits>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>

namespace math = nexenne::math;

TEST_CASE("strong angle types prevent unit confusion and convert") {
  constexpr math::degrees_d d{180.0};
  constexpr auto r{math::to_radians(d)};
  CHECK(r.value() == doctest::Approx(math::pi));
  CHECK(math::to_degrees(r).value() == doctest::Approx(180.0));

  // The raw-scalar convenience overloads.
  CHECK(math::to_radians(90.0).value() == doctest::Approx(math::half_pi));
  CHECK(math::to_degrees(math::half_pi).value() == doctest::Approx(90.0));
}

TEST_CASE("angle algebra is constexpr") {
  constexpr math::radians_d a{1.0};
  constexpr math::radians_d b{2.0};
  static_assert((a + b).value() == 3.0);
  static_assert((b - a).value() == 1.0);
  static_assert((-a).value() == -1.0);
  static_assert((a * 2.0).value() == 2.0);
  static_assert((2.0 * a).value() == 2.0);
  static_assert((b / 2.0).value() == 1.0);
  static_assert(b / a == 2.0);  // dimensionless ratio
  static_assert(a < b);
}

TEST_CASE("trig wrappers operate on radians") {
  CHECK(math::sin(math::radians_d{math::half_pi}) == doctest::Approx(1.0));
  CHECK(math::cos(math::radians_d{0.0}) == doctest::Approx(1.0));
  CHECK(math::tan(math::radians_d{0.0}) == doctest::Approx(0.0));
}

TEST_CASE("wrap_signed and wrap_unsigned reduce angles") {
  // 3pi wraps into [-pi, pi): 3pi - 2pi = pi, which maps to -pi.
  CHECK(math::wrap_signed(math::radians_d{3.0 * math::pi}).value() == doctest::Approx(-math::pi));
  CHECK(math::wrap_unsigned(math::radians_d{-1.0}).value() == doctest::Approx(math::tau - 1.0));
  auto const w{math::wrap_unsigned(math::radians_d{5.0 * math::pi}).value()};
  CHECK(w >= 0.0);
  CHECK(w < math::tau);
}

TEST_CASE("value_type alias is exposed") {
  static_assert(std::is_same_v<math::radians_f::value_type, float>);
  static_assert(std::is_same_v<math::degrees_d::value_type, double>);
}

TEST_CASE("wrap functions respect the half-open interval boundary (regression)") {
  // wrap_unsigned must never return exactly tau (the excluded upper bound): for a
  // sub-ulp negative input the naive value + tau rounds up to tau, which the
  // boundary guard pulls back to 0.
  CHECK(math::wrap_unsigned(math::radians_d{-1e-17}).value() == 0.0);
  CHECK(math::wrap_unsigned(math::radians_d{-1e-17}).value() < math::tau);
  // Floor-modulo reduction stays in range across magnitudes that still resolve
  // the period (up to ~1e9; beyond ~1e15 the input has no sub-period precision
  // left and no reduction can be meaningful). The old truncating long long cast
  // was undefined past ~5.8e19; this path never invokes that UB.
  for (double a : {1e6, -1e6, 1e9, -1e9, 1234.567, -987.654}) {
    auto const w{math::wrap_signed(math::radians_d{a}).value()};
    CHECK(w >= -math::pi);
    CHECK(w < math::pi);
    auto const u{math::wrap_unsigned(math::radians_d{a}).value()};
    CHECK(u >= 0.0);
    CHECK(u < math::tau);
  }
}
