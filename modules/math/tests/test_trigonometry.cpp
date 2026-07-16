#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/trigonometry.hpp>

namespace math = nexenne::math;

TEST_CASE("C1, M1, M2 combined reproduction (fails before, passes after)") {
  // C1: mod re-reduces a huge dividend into range; fast_sin no longer returns inf.
  constexpr auto tau{math::tau_v<double>};
  auto const residue{math::mod(6.3656990270058986e60, tau)};
  CHECK(residue >= 0.0);
  CHECK(residue < tau);
  CHECK(std::abs(math::fast_sin(math::radians_d{6.3656990270058986e60})) <= 1.0);

  // M1: fast_log on non-positive input returns like std::log instead of diverging.
  CHECK(math::fast_log(0.0) == -std::numeric_limits<double>::infinity());
  CHECK(std::isnan(math::fast_log(-1.0)));

  // M2: constexpr trunc for a huge long double compiles (2^63 <= 9.3e18 < 2^64).
  static_assert(math::trunc(9.3e18L) == 9.3e18L);
  CHECK(math::trunc(9.3e18L) == 9.3e18L);
}

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

TEST_CASE("lut_sin and fast_sin stay bounded for randomized huge angles (C1 regression)") {
  // The old single-pass mod left a huge negative residue above |a| > tau/epsilon,
  // so lut_sin returned garbage and fast_sin returned inf. Sweeping bit-mixed
  // mantissas above 1e17 (round decimals reduce cleanly and hide it) both entry
  // points must honour their [-1, 1] postcondition.
  std::uint64_t state{0xD1B54A32D192ED03ULL};
  auto const next{[&state]() noexcept -> std::uint64_t {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }};
  for (int e{60}; e <= 300; e += 3) {
    for (int trial{0}; trial < 100; ++trial) {
      auto const fraction{static_cast<double>(next() >> 11) / static_cast<double>(1ULL << 53)};
      auto const magnitude{std::ldexp(1.0 + fraction, e)};
      auto const a{(next() & 1U) != 0U ? magnitude : -magnitude};
      auto const s{math::fast_sin(math::radians_d{a})};
      CHECK(s >= -1.0);
      CHECK(s <= 1.0);
      auto const l{math::lut_sin(math::radians_d{a})};
      CHECK(l >= -1.0);
      CHECK(l <= 1.0);
    }
  }
  // The exact review probe: fast_sin returned inf before the mod fix.
  CHECK(math::fast_sin(math::radians_d{6.3656990270058986e60}) >= -1.0);
  CHECK(math::fast_sin(math::radians_d{6.3656990270058986e60}) <= 1.0);
}

TEST_CASE("lut_sin and lut_cos are usable in a constant expression (m9 regression)") {
  static_assert(
    math::lut_sin(math::radians_d{0.0}) == 0.0 || math::lut_sin(math::radians_d{0.0}) != 0.0
  );
  constexpr auto s{math::lut_sin(math::radians_f{1.5f})};
  constexpr auto c{math::lut_cos(math::radians_f{0.0f})};
  CHECK(s >= -1.0f);
  CHECK(c == doctest::Approx(1.0f).epsilon(1e-3));
}
