#include <doctest/doctest.h>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace math = nexenne::math;

TEST_CASE("dot, length, distance") {
  constexpr math::vector3_f a{1, 2, 2};
  static_assert(math::dot(a, a) == 9.0f);
  static_assert(math::length_squared(a) == 9.0f);
  CHECK(math::length(a) == doctest::Approx(3.0));
  static_assert(math::dot(math::vector2_f{1, 0}, math::vector2_f{0, 1}) == 0.0f);  // perpendicular

  constexpr math::vector3_f b{1, 2, 14};
  static_assert(math::distance_squared(a, b) == 144.0f);
  CHECK(math::distance(a, b) == doctest::Approx(12.0));
}

TEST_CASE("cross products and perpendicular") {
  // 3D cross of basis vectors: x cross y = z.
  constexpr math::vector3_f x{1, 0, 0};
  constexpr math::vector3_f y{0, 1, 0};
  static_assert(math::cross(x, y) == math::vector3_f{0, 0, 1});
  static_assert(math::cross(y, x) == math::vector3_f{0, 0, -1});  // anti-commutative

  // 2D scalar cross: sign gives orientation.
  static_assert(math::cross(math::vector2_f{1, 0}, math::vector2_f{0, 1}) == 1.0f);
  static_assert(math::perpendicular(math::vector2_f{1, 0}) == math::vector2_f{0, 1});
}

TEST_CASE("normalize variants") {
  constexpr math::vector3_f v{3, 0, 4};  // length 5
  auto const n{math::normalize(v)};
  REQUIRE(n.has_value());
  CHECK(math::length(*n) == doctest::Approx(1.0));
  CHECK(n->x() == doctest::Approx(0.6));

  // Zero vector fails.
  auto const z{math::normalize(math::vector3_f{0, 0, 0})};
  REQUIRE_FALSE(z.has_value());
  CHECK(z.error() == math::math_error::zero_length_vector);

  // fast_normalize: unit length within fast_inv_sqrt tolerance.
  auto const fn{math::fast_normalize(v)};
  REQUIRE(fn.has_value());
  CHECK(math::length(*fn) == doctest::Approx(1.0).epsilon(1e-4));

  // normalize_or returns the fallback for a zero vector.
  constexpr math::vector3_f up{0, 1, 0};
  CHECK(math::normalize_or(math::vector3_f{0, 0, 0}, up) == up);
}

TEST_CASE("projection, rejection, reflection") {
  constexpr math::vector2_f v{3, 4};
  constexpr math::vector2_f axis{1, 0};
  auto const p{math::project(v, axis)};
  REQUIRE(p.has_value());
  CHECK(*p == math::vector2_f{3, 0});
  auto const r{math::reject(v, axis)};
  REQUIRE(r.has_value());
  CHECK(*r == math::vector2_f{0, 4});

  // Reflect about the y-axis normal flips x.
  constexpr math::vector2_f normal{1, 0};
  static_assert(math::reflect(math::vector2_f{1, -1}, normal) == math::vector2_f{-1, -1});
}

TEST_CASE("element-wise lerp, clamp, min/max, hadamard") {
  constexpr math::vector3_f a{0, 0, 0};
  constexpr math::vector3_f b{10, 20, 30};
  static_assert(math::lerp(a, b, 0.5f) == math::vector3_f{5, 10, 15});
  static_assert(math::clamp(math::vector3_f{-1, 5, 100}, a, b) == math::vector3_f{0, 5, 30});
  static_assert(math::component_min(b, math::vector3_f{5, 25, 15}) == math::vector3_f{5, 20, 15});
  static_assert(math::component_max(b, math::vector3_f{5, 25, 15}) == math::vector3_f{10, 25, 30});
  static_assert(
    math::hadamard(math::vector3_f{2, 3, 4}, math::vector3_f{5, 6, 7})
    == math::vector3_f{10, 18, 28}
  );
}

TEST_CASE("reductions and alternative norms") {
  constexpr math::vector4_f v{1, 2, 3, 4};
  static_assert(math::sum(v) == 10.0f);
  static_assert(math::product(v) == 24.0f);
  static_assert(math::min_component(v) == 1.0f);
  static_assert(math::max_component(v) == 4.0f);
  static_assert(math::manhattan_length(math::vector3_f{1, -2, 3}) == 6.0f);
  static_assert(math::chebyshev_length(math::vector3_f{1, -5, 3}) == 5.0f);
}

TEST_CASE("angle_between") {
  constexpr math::vector2_f x{1, 0};
  constexpr math::vector2_f y{0, 1};
  auto const right{math::angle_between(x, y)};
  REQUIRE(right.has_value());
  CHECK(*right == doctest::Approx(math::half_pi));

  auto const same{math::angle_between(x, x)};
  REQUIRE(same.has_value());
  CHECK(*same == doctest::Approx(0.0).epsilon(1e-6));

  auto const opp{math::angle_between(x, math::vector2_f{-1, 0})};
  REQUIRE(opp.has_value());
  CHECK(*opp == doctest::Approx(math::pi));

  CHECK_FALSE(math::angle_between(x, math::vector2_f{0, 0}).has_value());
}

TEST_CASE("almost_equal and move_toward for vectors") {
  CHECK(math::almost_equal(math::vector3_f{1, 2, 3}, math::vector3_f{1, 2, 3 + 1e-8f}));
  CHECK_FALSE(math::almost_equal(math::vector3_f{1, 2, 3}, math::vector3_f{1, 2, 3.1f}));

  constexpr math::vector2_f start{0, 0};
  constexpr math::vector2_f target{10, 0};
  CHECK(math::move_toward(start, target, 3.0f) == math::vector2_f{3, 0});
  CHECK(math::move_toward(start, target, 100.0f) == target);  // snaps
}

TEST_CASE("normalized wrapper carries the unit-length guarantee") {
  constexpr math::vector3_f v{3, 0, 4};
  auto const n{math::make_normalized(v)};
  REQUIRE(n.has_value());
  CHECK(math::length(n->value()) == doctest::Approx(1.0));
  static_assert(std::is_same_v<math::normalized<float, 3>::vector_type, math::vector3_f>);

  // The implicit conversion works in non-template contexts.
  math::vector3_f back = *n;
  CHECK(math::length(back) == doctest::Approx(1.0));

  // make_unchecked trusts a known unit vector.
  constexpr auto up{math::make_unchecked(math::vector3_f{0, 1, 0})};
  CHECK(up.value().y() == 1.0f);

  // A zero vector cannot be made normalized.
  CHECK_FALSE(math::make_normalized(math::vector3_f{0, 0, 0}).has_value());
}

TEST_CASE("angle_between guards each length separately (regression)") {
  // Two genuinely-nonzero short vectors must not be rejected as zero-length: the
  // old product-of-squared-lengths threshold wrongly flagged them.
  auto const r{math::angle_between(math::vector3_d{1e-8, 0, 0}, math::vector3_d{0, 1e-8, 0})};
  REQUIRE(r.has_value());
  CHECK(*r == doctest::Approx(math::half_pi));
}

TEST_CASE("vector move_toward does not move for a non-positive step (regression)") {
  auto const r{math::move_toward(math::vector3_d{0, 0, 0}, math::vector3_d{10, 0, 0}, -3.0)};
  CHECK(r.x() == 0.0);
  CHECK(r.y() == 0.0);
  CHECK(r.z() == 0.0);
}
