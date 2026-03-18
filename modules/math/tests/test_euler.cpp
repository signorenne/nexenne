#include <doctest/doctest.h>

#include <cmath>

#include <nexenne/math/euler.hpp>

namespace math = nexenne::math;

namespace {
auto same_orientation(math::quaternion_d const& a, math::quaternion_d const& b) -> bool {
  return std::abs(math::dot(a, b)) == doctest::Approx(1.0);  // equal up to sign
}
}  // namespace

TEST_CASE("single-axis euler matches from_axis_angle") {
  // A pure X rotation must equal a from_axis_angle about +X.
  auto const ex{
    math::to_quaternion(math::euler_angles<double>{0.7, 0.0, 0.0}, math::euler_order::xyz)
  };
  auto const ax{*math::from_axis_angle(math::vector3_d{1, 0, 0}, math::radians_d{0.7})};
  CHECK(same_orientation(ex, ax));
  CHECK(math::length(ex) == doctest::Approx(1.0));
}

TEST_CASE("order matters: xyz differs from zyx for the same angles") {
  math::euler_angles<double> const a{0.3, 0.5, 0.7};
  auto const q_xyz{math::to_quaternion(a, math::euler_order::xyz)};
  auto const q_zyx{math::to_quaternion(a, math::euler_order::zyx)};
  CHECK_FALSE(same_orientation(q_xyz, q_zyx));
  CHECK(math::length(q_xyz) == doctest::Approx(1.0));
  CHECK(math::length(q_zyx) == doctest::Approx(1.0));
}

TEST_CASE("from_ypr equals intrinsic zyx and quaternion::from_euler") {
  double const yaw{0.4};
  double const pitch{-0.2};
  double const roll{0.9};
  auto const ypr{math::from_ypr(yaw, pitch, roll)};
  auto const zyx{
    math::to_quaternion(math::euler_angles<double>{roll, pitch, yaw}, math::euler_order::zyx)
  };
  CHECK(same_orientation(ypr, zyx));

  // And it matches the dedicated quaternion::from_euler (same ZYX convention).
  auto const direct{
    math::from_euler(math::radians_d{roll}, math::radians_d{pitch}, math::radians_d{yaw})
  };
  CHECK(same_orientation(ypr, direct));
}

TEST_CASE("the orders are intrinsic, not extrinsic") {
  // Intrinsic xyz of (90 deg, 0, 90 deg) applied to (1,0,0) gives (0,0,1).
  // Genuine extrinsic xyz would give (0,1,0); this pins the documented intrinsic
  // convention so it cannot silently flip.
  auto const q{math::to_quaternion(
    math::euler_angles<double>{math::half_pi, 0.0, math::half_pi}, math::euler_order::xyz
  )};
  auto const v{math::rotate(q, math::vector3_d{1, 0, 0})};
  CHECK(std::abs(v.x()) < 1e-9);
  CHECK(std::abs(v.y()) < 1e-9);
  CHECK(v.z() == doctest::Approx(1.0));
}

TEST_CASE("all six orders produce unit quaternions") {
  math::euler_angles<double> const a{0.2, 0.4, 0.6};
  for (auto order :
       {math::euler_order::xyz,
        math::euler_order::xzy,
        math::euler_order::yxz,
        math::euler_order::yzx,
        math::euler_order::zxy,
        math::euler_order::zyx}) {
    CHECK(math::length(math::to_quaternion(a, order)) == doctest::Approx(1.0));
  }
}
