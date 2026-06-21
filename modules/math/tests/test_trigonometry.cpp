#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/trigonometry.hpp>

namespace math = nexenne::math;

TEST_CASE("sincos returns a matching pair") {
  auto const sc{math::sincos(math::radians_d{math::third_pi})};
  CHECK(sc.sin() == doctest::Approx(std::sin(math::third_pi)));
  CHECK(sc.cos() == doctest::Approx(std::cos(math::third_pi)));
  static_assert(std::is_same_v<math::sin_cos<double>::value_type, double>);
}

TEST_CASE("fast_sin and fast_cos approximate libm and are constexpr") {
  for (double a : {-3.0, -1.0, 0.0, 0.5, 1.5, 3.0, 6.0}) {
    CHECK(math::fast_sin(math::radians_d{a}) == doctest::Approx(std::sin(a)).epsilon(1e-5));
    CHECK(math::fast_cos(math::radians_d{a}) == doctest::Approx(std::cos(a)).epsilon(1e-5));
  }
  static_assert(math::fast_cos(math::radians_d{0.0}) == 1.0);
  // The paired form agrees with the singles.
  constexpr auto sc{math::fast_sincos(math::radians_d{1.0})};
  CHECK(sc.sin() == doctest::Approx(math::fast_sin(math::radians_d{1.0})));
}

TEST_CASE("fast inverse trig approximates libm") {
  for (double x : {-0.9, -0.5, 0.0, 0.5, 0.9}) {
    CHECK(math::fast_asin(x).value() == doctest::Approx(std::asin(x)).epsilon(1e-4));
    CHECK(math::fast_acos(x).value() == doctest::Approx(std::acos(x)).epsilon(1e-4));
  }
  for (double x : {-5.0, -1.0, 0.0, 1.0, 5.0}) {
    CHECK(math::fast_atan(x).value() == doctest::Approx(std::atan(x)).epsilon(1e-4));
  }
  // atan2 quadrants.
  CHECK(math::fast_atan2(1.0, 1.0).value() == doctest::Approx(std::atan2(1.0, 1.0)).epsilon(1e-4));
  CHECK(
    math::fast_atan2(1.0, -1.0).value() == doctest::Approx(std::atan2(1.0, -1.0)).epsilon(1e-4)
  );
  CHECK(
    math::fast_atan2(-1.0, -1.0).value() == doctest::Approx(std::atan2(-1.0, -1.0)).epsilon(1e-4)
  );
  CHECK(math::fast_atan2(0.0, 0.0).value() == 0.0);  // documented zero case
}

TEST_CASE("lut_sin and lut_cos approximate libm") {
  for (double a : {0.0, 0.5, 1.5, 3.0, 5.0, 7.0}) {
    CHECK(math::lut_sin(math::radians_d{a}) == doctest::Approx(std::sin(a)).epsilon(1e-3));
    CHECK(math::lut_cos(math::radians_d{a}) == doctest::Approx(std::cos(a)).epsilon(1e-3));
  }
}

TEST_CASE("angle_diff and lerp_angle take the short way around") {
  // Difference between 350 deg and 10 deg is -20 deg, not +340.
  auto const a{math::radians_d{math::to_radians(350.0).value()}};
  auto const b{math::radians_d{math::to_radians(10.0).value()}};
  CHECK(math::angle_diff(a, b).value() == doctest::Approx(math::to_radians(-20.0).value()));

  // Halfway from 350 to 10 deg is 0 deg (wrapping), not 180.
  auto const mid{math::lerp_angle(a, b, 0.5).value()};
  CHECK(std::abs(mid) < 1e-9);
}

TEST_CASE("lut/fast trig survive extreme and out-of-domain inputs (regression)") {
  // Huge angles must not overflow the reduction; result stays in [-1, 1].
  CHECK(math::lut_sin(math::radians_d{1e30}) >= -1.0);
  CHECK(math::lut_sin(math::radians_d{1e30}) <= 1.0);
  // fast_asin/acos clamp marginally-out-of-range inputs instead of returning NaN.
  CHECK_FALSE(std::isnan(math::fast_asin(1.0000000001).value()));
  CHECK(math::fast_asin(1.0000000001).value() == doctest::Approx(math::half_pi));
  CHECK_FALSE(std::isnan(math::fast_acos(-1.0000000001).value()));
  // fast_atan2 matches IEEE std::atan2 on a negative zero.
  CHECK(math::fast_atan2(-0.0, -1.0).value() == doctest::Approx(std::atan2(-0.0, -1.0)));
}

TEST_CASE("poly-based fast trig survives out-of-contract huge angles (regression)") {
  // The round_nearest cast would be UB past the long long range; the guarded
  // pre-reduction keeps fast_sin/cos defined and bounded for enormous inputs.
  CHECK(math::fast_sin(math::radians_d{1e30}) >= -1.0);
  CHECK(math::fast_sin(math::radians_d{1e30}) <= 1.0);
  CHECK(math::fast_cos(math::radians_d{-1e30}) >= -1.0);
  CHECK(math::fast_cos(math::radians_d{-1e30}) <= 1.0);
}
