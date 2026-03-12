#include <doctest/doctest.h>

#include <numbers>

#include <nexenne/math/concepts.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/error.hpp>

namespace math = nexenne::math;

TEST_CASE("arithmetic concepts") {
  static_assert(math::arithmetic<int>);
  static_assert(math::arithmetic<double>);
  static_assert(!math::arithmetic<int*>);
  static_assert(math::signed_arithmetic<int>);
  static_assert(math::signed_arithmetic<double>);
  static_assert(!math::signed_arithmetic<unsigned>);
}

TEST_CASE("constants bind to the scalar type and to double") {
  static_assert(math::pi_v<float> == std::numbers::pi_v<float>);
  static_assert(math::tau_v<double> == 2.0 * std::numbers::pi);
  static_assert(math::pi == std::numbers::pi);
  static_assert(math::half_pi_v<double> == std::numbers::pi / 2.0);

  // Exact closed-form trig values.
  static_assert(math::sin_pi_6_v<double> == 0.5);
  static_assert(math::cos_pi_3_v<double> == 0.5);
  CHECK(math::cos_pi_4_v<double> == doctest::Approx(math::sin_pi_4_v<double>));

  // Conversion factors round-trip.
  CHECK(math::deg_to_rad * 180.0 == doctest::Approx(std::numbers::pi));
  CHECK(math::rad_to_deg * std::numbers::pi == doctest::Approx(180.0));
}

TEST_CASE("math_error names round-trip through to_string") {
  CHECK(math::to_string(math::math_error::zero_length_vector) == "zero_length_vector");
  CHECK(math::to_string(math::math_error::singular_matrix) == "singular_matrix");
  CHECK(math::to_string(math::math_error::parallel_vectors) == "parallel_vectors");
  CHECK(math::to_string(math::math_error::invalid_input) == "invalid_input");
}

TEST_CASE("result carries either a value or an error") {
  math::result<int> ok{42};
  REQUIRE(ok.has_value());
  CHECK(*ok == 42);

  math::result<int> bad{std::unexpected{math::math_error::invalid_input}};
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == math::math_error::invalid_input);
}
