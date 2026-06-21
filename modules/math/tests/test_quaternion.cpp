#include <doctest/doctest.h>

#include <cmath>
#include <type_traits>

#include <nexenne/math/quaternion.hpp>

namespace math = nexenne::math;

namespace {
auto vapprox(math::vector3_d const& a, math::vector3_d const& b, double eps = 1e-9) -> bool {
  return std::abs(a.x() - b.x()) < eps && std::abs(a.y() - b.y()) < eps
         && std::abs(a.z() - b.z()) < eps;
}
}  // namespace

TEST_CASE("layout, identity, deduction") {
  static_assert(std::is_trivially_copyable_v<math::quaternion_f>);
  static_assert(sizeof(math::quaternion_f) == 4 * sizeof(float));
  static_assert(std::is_same_v<math::quaternion_f::value_type, float>);

  constexpr auto id{math::quaternion_d::identity()};
  static_assert(id.w() == 1.0 && id.x() == 0.0 && id.y() == 0.0 && id.z() == 0.0);

  constexpr auto q{math::quaternion{1.0, 2.0, 3.0, 4.0}};  // deduced
  static_assert(std::is_same_v<decltype(q), math::quaternion<double> const>);
  static_assert(q.x() == 1.0 && q.w() == 4.0);
}

TEST_CASE("Hamilton product, conjugate, inverse, dot, norm") {
  // i * j = k : quaternion{1,0,0,0} * {0,1,0,0} = {0,0,1,0}
  constexpr math::quaternion_d i{1, 0, 0, 0};
  constexpr math::quaternion_d j{0, 1, 0, 0};
  static_assert((i * j) == math::quaternion_d{0, 0, 1, 0});

  constexpr auto id{math::quaternion_d::identity()};
  constexpr math::quaternion_d q{0.5, 0.5, 0.5, 0.5};  // unit
  static_assert(math::length_squared(q) == 1.0);
  CHECK(math::length(q) == doctest::Approx(1.0));
  CHECK(math::dot(q, q) == doctest::Approx(1.0));

  // For a unit quaternion, conjugate == inverse, and q * conj(q) == identity.
  auto const inv{math::inverse(q)};
  REQUIRE(inv.has_value());
  CHECK(*inv == math::conjugate(q));
  auto const prod{q * *inv};
  CHECK(prod.w() == doctest::Approx(1.0));
  CHECK(prod.x() == doctest::Approx(0.0));
  static_assert(id == id);  // silence unused
}

TEST_CASE("from_axis_angle and rotate a vector") {
  // 90 degrees about +Z takes +X to +Y.
  auto const q{math::from_axis_angle(math::vector3_d{0, 0, 1}, math::radians_d{math::half_pi})};
  REQUIRE(q.has_value());
  CHECK(math::length(*q) == doctest::Approx(1.0));
  auto const rotated{math::rotate(*q, math::vector3_d{1, 0, 0})};
  CHECK(vapprox(rotated, math::vector3_d{0, 1, 0}));

  // A zero axis is rejected.
  CHECK_FALSE(math::from_axis_angle(math::vector3_d{0, 0, 0}, math::radians_d{1.0}).has_value());
}

TEST_CASE("to_axis_angle inverts from_axis_angle") {
  auto const q{math::from_axis_angle(math::vector3_d{0, 1, 0}, math::radians_d{1.2})};
  REQUIRE(q.has_value());
  auto const aa{math::to_axis_angle(*q)};
  CHECK(aa.angle().value() == doctest::Approx(1.2));
  CHECK(vapprox(aa.axis(), math::vector3_d{0, 1, 0}));

  // Identity quaternion gives angle 0 and the default +X axis.
  auto const idaa{math::to_axis_angle(math::quaternion_d::identity())};
  CHECK(idaa.angle().value() == doctest::Approx(0.0));
  CHECK(vapprox(idaa.axis(), math::vector3_d{1, 0, 0}));
}

TEST_CASE("from_two_vectors rotates from onto to, incl. antipodal") {
  auto const q{math::from_two_vectors(math::vector3_d{1, 0, 0}, math::vector3_d{0, 1, 0})};
  REQUIRE(q.has_value());
  CHECK(vapprox(math::rotate(*q, math::vector3_d{1, 0, 0}), math::vector3_d{0, 1, 0}));

  // Antipodal: +X to -X. Result still rotates one onto the other.
  auto const anti{math::from_two_vectors(math::vector3_d{1, 0, 0}, math::vector3_d{-1, 0, 0})};
  REQUIRE(anti.has_value());
  CHECK(vapprox(math::rotate(*anti, math::vector3_d{1, 0, 0}), math::vector3_d{-1, 0, 0}, 1e-6));
}

TEST_CASE("to_matrix3 agrees with rotate") {
  auto const q{math::from_axis_angle(math::vector3_d{1, 1, 1}, math::radians_d{0.7})};
  REQUIRE(q.has_value());
  auto const m{math::to_matrix3(*q)};
  math::vector3_d const v{0.3, -0.5, 0.8};
  auto const by_matrix{m * v};
  auto const by_rotate{math::rotate(*q, v)};
  CHECK(vapprox(by_matrix, by_rotate, 1e-9));

  // to_matrix4 has the same upper-left block and an identity last row/col.
  auto const m4{math::to_matrix4(*q)};
  CHECK(m4(0, 0) == doctest::Approx(m(0, 0)));
  CHECK(m4(3, 3) == doctest::Approx(1.0));
  CHECK(m4(0, 3) == doctest::Approx(0.0));
}

TEST_CASE("look_at_rotation maps -Z onto forward (right-handed)") {
  // The documented convention: the rotation takes the -Z axis onto forward.
  for (auto const& forward :
       {math::vector3_d{0, 0, -1}, math::vector3_d{1, 0, 0}, math::vector3_d{1, 2, 3}}) {
    auto const q{math::look_at_rotation(forward, math::vector3_d{0, 1, 0})};
    REQUIRE(q.has_value());
    CHECK(math::length(*q) == doctest::Approx(1.0));
    auto const f_unit{*math::normalize(forward)};
    CHECK(vapprox(math::rotate(*q, math::vector3_d{0, 0, -1}), f_unit, 1e-9));
  }
  // Parallel forward/up is rejected.
  CHECK_FALSE(math::look_at_rotation(math::vector3_d{0, 1, 0}, math::vector3_d{0, 1, 0}).has_value()
  );
}

TEST_CASE("nlerp and slerp endpoints and unit length") {
  auto const a{*math::from_axis_angle(math::vector3_d{0, 0, 1}, math::radians_d{0.0})};
  auto const b{*math::from_axis_angle(math::vector3_d{0, 0, 1}, math::radians_d{math::half_pi})};

  CHECK(math::dot(math::slerp(a, b, 0.0), a) == doctest::Approx(1.0));
  CHECK(math::dot(math::slerp(a, b, 1.0), b) == doctest::Approx(1.0));
  CHECK(math::length(math::slerp(a, b, 0.5)) == doctest::Approx(1.0));
  CHECK(math::length(math::nlerp(a, b, 0.5)) == doctest::Approx(1.0));

  // slerp midpoint is the 45-degree rotation about Z.
  auto const mid{math::slerp(a, b, 0.5)};
  auto const expected{
    *math::from_axis_angle(math::vector3_d{0, 0, 1}, math::radians_d{math::half_pi / 2})
  };
  CHECK(std::abs(math::dot(mid, expected)) == doctest::Approx(1.0));
}

TEST_CASE("from_two_vectors is accurate in the near-antipodal gap (regression)") {
  math::vector3_d const from{1, 0, 0};
  // ~179.84 degrees: inside the band where the old form snapped to an exact 180
  // and lost ~3e-3 of accuracy; the half-vector method must be exact here.
  double const ang{math::pi * 0.9991};
  math::vector3_d const to{std::cos(ang), std::sin(ang), 0};
  auto const q{math::from_two_vectors(from, to)};
  REQUIRE(q.has_value());
  CHECK(vapprox(math::rotate(*q, from), *math::normalize(to), 1e-9));
}
