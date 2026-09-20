#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
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
  static_assert(b / a == 2.0);
  static_assert(a < b);
}

TEST_CASE("angle compound assignment mutates in place") {
  auto heading{math::radians_d{1.0}};
  heading += math::radians_d{0.5};
  CHECK(heading.value() == doctest::Approx(1.5));
  heading -= math::radians_d{0.25};
  CHECK(heading.value() == doctest::Approx(1.25));
  heading *= 2.0;
  CHECK(heading.value() == doctest::Approx(2.5));
  heading /= 5.0;
  CHECK(heading.value() == doctest::Approx(0.5));

  auto spin{math::degrees_f{90.0f}};
  spin += math::degrees_f{45.0f};
  CHECK(spin.value() == doctest::Approx(135.0));
  spin /= 3.0f;
  CHECK(spin.value() == doctest::Approx(45.0));

  static_assert([] {
    auto a{math::radians_d{2.0}};
    a += math::radians_d{1.0};  // 3.0
    a -= math::radians_d{0.5};  // 2.5
    a *= 2.0;                   // 5.0
    a /= 5.0;                   // 1.0
    return a.value();
  }() == 1.0);
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

TEST_CASE("wrap functions respect the half-open interval boundary") {
  // -1e-17 + tau rounds up to tau, the excluded upper bound.
  CHECK(math::wrap_unsigned(math::radians_d{-1e-17}).value() == 0.0);
  CHECK(math::wrap_unsigned(math::radians_d{-1e-17}).value() < math::tau);
  for (double a : {1e6, -1e6, 1e9, -1e9, 1234.567, -987.654}) {
    auto const w{math::wrap_signed(math::radians_d{a}).value()};
    CHECK(w >= -math::pi);
    CHECK(w < math::pi);
    auto const u{math::wrap_unsigned(math::radians_d{a}).value()};
    CHECK(u >= 0.0);
    CHECK(u < math::tau);
  }
}

TEST_CASE("wrap_signed and wrap_unsigned stay in range for randomized huge angles") {
  // Bit-mixed mantissas: round decimals reduce cleanly and would hide a bad residue.
  std::uint64_t state{0x2545F4914F6CDD1DULL};
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
      auto const w{math::wrap_signed(math::radians_d{a}).value()};
      CHECK(w >= -math::pi);
      CHECK(w < math::pi);
      auto const u{math::wrap_unsigned(math::radians_d{a}).value()};
      CHECK(u >= 0.0);
      CHECK(u < math::tau);
    }
  }
}
