#include <doctest/doctest.h>

#include <limits>

#include <nexenne/math/scalar.hpp>

namespace math = nexenne::math;

TEST_CASE("selection: min, max, clamp, saturate, step") {
  static_assert(math::min(3, 7) == 3);
  static_assert(math::max(3, 7) == 7);
  static_assert(math::clamp(5, 0, 10) == 5);
  static_assert(math::clamp(-1, 0, 10) == 0);
  static_assert(math::clamp(11, 0, 10) == 10);
  static_assert(math::saturate(1.5) == 1.0);
  static_assert(math::saturate(-0.5) == 0.0);
  static_assert(math::step(0.5, 0.4) == 0.0);
  static_assert(math::step(0.5, 0.6) == 1.0);
}

TEST_CASE("clamp propagates NaN") {
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  CHECK(math::isnan(math::clamp(nan, 0.0, 1.0)));
}

TEST_CASE("move_toward steps by at most max_delta") {
  static_assert(math::move_toward(0.0, 10.0, 3.0) == 3.0);
  static_assert(math::move_toward(0.0, 2.0, 3.0) == 2.0);   // snaps to target
  static_assert(math::move_toward(10.0, 0.0, 3.0) == 7.0);  // descends
}

TEST_CASE("sign and magnitude") {
  static_assert(math::abs(-4) == 4);
  static_assert(math::abs(4.0) == 4.0);
  static_assert(math::sign(-3.0) == -1.0);
  static_assert(math::sign(0.0) == 0.0);
  static_assert(math::sign(3) == 1);
  static_assert(math::copysign(3.0, -1.0) == -3.0);
  static_assert(math::copysign(-3.0, 2.0) == 3.0);
  static_assert(math::square(5) == 25);
  static_assert(math::cube(-2.0) == -8.0);
}

TEST_CASE("interpolation: lerp, inverse_lerp, remap, smoothstep") {
  static_assert(math::lerp(0.0, 10.0, 0.5) == 5.0);
  static_assert(math::lerp(0.0, 10.0, 0.0) == 0.0);
  static_assert(math::lerp(0.0, 10.0, 1.0) == 10.0);
  static_assert(math::inverse_lerp(0.0, 10.0, 5.0) == 0.5);
  static_assert(math::remap(5.0, 0.0, 10.0, 0.0, 100.0) == 50.0);
  static_assert(math::smoothstep(0.0, 1.0, 0.0) == 0.0);
  static_assert(math::smoothstep(0.0, 1.0, 1.0) == 1.0);
  static_assert(math::smoothstep(0.0, 1.0, 0.5) == 0.5);  // symmetric midpoint
}

TEST_CASE("equality and classification") {
  static_assert(math::almost_equal(1.0, 1.0 + 1e-9));
  static_assert(!math::almost_equal(1.0, 1.1));
  static_assert(math::approximately_zero(1e-9));
  static_assert(math::isnan(std::numeric_limits<double>::quiet_NaN()));
  static_assert(math::isinf(std::numeric_limits<double>::infinity()));
  static_assert(!math::isfinite(std::numeric_limits<double>::infinity()));
  static_assert(math::isfinite(1.0));
}

TEST_CASE("rounding works at compile time and runtime") {
  static_assert(math::trunc(2.7) == 2.0);
  static_assert(math::trunc(-2.7) == -2.0);
  static_assert(math::floor(2.7) == 2.0);
  static_assert(math::floor(-2.1) == -3.0);
  static_assert(math::ceil(2.1) == 3.0);
  static_assert(math::ceil(-2.7) == -2.0);
  static_assert(math::round(2.5) == 3.0);
  static_assert(math::round(-2.5) == -3.0);

  // Runtime path dispatches to libm; results must match.
  CHECK(math::floor(2.7) == 2.0);
  CHECK(math::ceil(-2.7) == -2.0);
  CHECK(math::round(2.5) == 3.0);
  CHECK(math::fract(2.25) == doctest::Approx(0.25));
}

TEST_CASE("floor-based modulo and wrapping") {
  // mod takes the sign of the divisor (Python convention).
  static_assert(math::mod(5.0, 3.0) == 2.0);
  CHECK(math::mod(-1.0, 3.0) == doctest::Approx(2.0));
  CHECK(math::repeat(7.5, 5.0) == doctest::Approx(2.5));
  CHECK(math::wrap(12.0, 10.0, 20.0) == doctest::Approx(12.0));
  CHECK(math::wrap(22.0, 10.0, 20.0) == doctest::Approx(12.0));

  // ping_pong is a triangle wave bouncing in [0, length].
  CHECK(math::ping_pong(0.0, 5.0) == doctest::Approx(0.0));
  CHECK(math::ping_pong(5.0, 5.0) == doctest::Approx(5.0));
  CHECK(math::ping_pong(7.0, 5.0) == doctest::Approx(3.0));
  CHECK(math::ping_pong(10.0, 5.0) == doctest::Approx(0.0));
}

TEST_CASE("round does not double-round near 0.5 (regression)") {
  // The largest double strictly below 0.5 must round to 0, not 1.
  static_assert(math::round(0.49999999999999994) == 0.0);
  static_assert(math::round(-0.49999999999999994) == 0.0);
  static_assert(math::round(0.5) == 1.0);
  static_assert(math::round(-0.5) == -1.0);
}

TEST_CASE("trunc does not overflow for huge consteval inputs (regression)") {
  // Values beyond long long range are already integral; no UB cast.
  static_assert(math::trunc(1e30) == 1e30);
  static_assert(math::floor(1e30) == 1e30);
  static_assert(math::trunc(-1e30) == -1e30);
}

TEST_CASE("fract and mod respect the half-open boundary for tiny-negative input (regression)") {
  // value - floor(value) rounds up to exactly 1 here; must clamp back to 0.
  CHECK(math::fract(-1e-20) == 0.0);
  CHECK(math::fract(-1e-20) < 1.0);
  CHECK(math::mod(-1e-20, 5.0) == 0.0);
  CHECK(math::repeat(-1e-20, 5.0) < 5.0);
  CHECK(math::wrap(-1e-20, 0.0, 10.0) < 10.0);
}

TEST_CASE("move_toward does not move for a non-positive step (regression)") {
  // A negative max_delta must not step away from the target.
  static_assert(math::move_toward(0.0, 10.0, -3.0) == 0.0);
  static_assert(math::move_toward(0.0, 10.0, 0.0) == 0.0);
  static_assert(math::move_toward(5.0, 5.0, -1.0) == 5.0);
}
