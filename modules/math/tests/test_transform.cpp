#include <doctest/doctest.h>

#include <cmath>

#include <nexenne/math/transform.hpp>

namespace math = nexenne::math;

namespace {
auto vapprox(math::vector3_d const& a, math::vector3_d const& b, double eps = 1e-9) -> bool {
  return std::abs(a.x() - b.x()) < eps && std::abs(a.y() - b.y()) < eps
         && std::abs(a.z() - b.z()) < eps;
}
}  // namespace

TEST_CASE("translation and scale builders place their values correctly") {
  constexpr auto t3{math::translation3(math::vector3_d{1, 2, 3})};
  static_assert(t3(0, 3) == 1.0 && t3(1, 3) == 2.0 && t3(2, 3) == 3.0);
  static_assert(t3(3, 3) == 1.0);

  constexpr auto s3{math::scale3(math::vector3_d{2, 3, 4})};
  static_assert(s3(0, 0) == 2.0 && s3(1, 1) == 3.0 && s3(2, 2) == 4.0);

  constexpr auto t2{math::translation2(math::vector2_d{5, 6})};
  static_assert(t2(0, 2) == 5.0 && t2(1, 2) == 6.0 && t2(2, 2) == 1.0);

  constexpr auto s2{math::scale2(math::vector2_d{7, 8})};
  static_assert(s2(0, 0) == 7.0 && s2(1, 1) == 8.0);
}

TEST_CASE("rotation2 rotates a vector counter-clockwise") {
  // 90 degrees CCW takes (1, 0) to (0, 1) in the upper-left 2x2 block.
  auto const r{math::rotation2(math::radians_d{math::half_pi})};
  CHECK(r(0, 0) == doctest::Approx(0.0));
  CHECK(r(1, 0) == doctest::Approx(1.0));
  CHECK(r(0, 1) == doctest::Approx(-1.0));
  CHECK(r(1, 1) == doctest::Approx(0.0));
}

TEST_CASE("rotation3 and rotation3_axis_angle agree with the quaternion") {
  auto const q{math::from_axis_angle(math::vector3_d{0, 0, 1}, math::radians_d{0.7})};
  REQUIRE(q.has_value());
  auto const m_q{math::rotation3(*q)};
  auto const m_aa{math::rotation3_axis_angle(math::vector3_d{0, 0, 1}, math::radians_d{0.7})};
  REQUIRE(m_aa.has_value());
  for (std::size_t r{0}; r < 4; ++r) {
    for (std::size_t c{0}; c < 4; ++c) {
      CHECK(m_q(r, c) == doctest::Approx((*m_aa)(r, c)));
    }
  }
  // A zero axis is rejected.
  CHECK_FALSE(math::rotation3_axis_angle(math::vector3_d{0, 0, 0}, math::radians_d{1.0}).has_value()
  );
}

TEST_CASE("look_at maps the eye to the origin and the view direction to -Z") {
  auto const view{
    math::look_at(math::vector3_d{0, 0, 5}, math::vector3_d{0, 0, 0}, math::vector3_d{0, 1, 0})
  };
  REQUIRE(view.has_value());
  // The eye lands on the origin.
  CHECK(vapprox(math::transform_point(*view, math::vector3_d{0, 0, 5}), math::vector3_d{0, 0, 0}));
  // The target sits straight ahead on -Z, five units away.
  auto const t{math::transform_point(*view, math::vector3_d{0, 0, 0})};
  CHECK(t.x() == doctest::Approx(0.0));
  CHECK(t.y() == doctest::Approx(0.0));
  CHECK(t.z() == doctest::Approx(-5.0));

  // Coincident eye/target and a parallel up are reported, not crashed on.
  CHECK_FALSE(
    math::look_at(math::vector3_d{1, 1, 1}, math::vector3_d{1, 1, 1}, math::vector3_d{0, 1, 0})
      .has_value()
  );
  CHECK_FALSE(
    math::look_at(math::vector3_d{0, 0, 0}, math::vector3_d{0, 0, -1}, math::vector3_d{0, 0, 1})
      .has_value()
  );
}

TEST_CASE("transform_point applies the perspective divide; direction ignores translation") {
  // Bottom row (0, 0, 1, 0) makes the homogeneous w equal to z, forcing a divide.
  constexpr auto m{math::make_matrix4<double>(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0)};
  CHECK(vapprox(math::transform_point(m, math::vector3_d{2, 4, 8}), math::vector3_d{0.25, 0.5, 1.0})
  );

  // A pure translation moves a point but leaves a direction untouched.
  constexpr auto t{math::translation3(math::vector3_d{10, 20, 30})};
  CHECK(vapprox(math::transform_point(t, math::vector3_d{1, 1, 1}), math::vector3_d{11, 21, 31}));
  CHECK(vapprox(math::transform_direction(t, math::vector3_d{1, 1, 1}), math::vector3_d{1, 1, 1}));
}
