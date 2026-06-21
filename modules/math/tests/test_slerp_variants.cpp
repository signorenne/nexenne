#include <doctest/doctest.h>

#include <cmath>

#include <nexenne/math/slerp_variants.hpp>

namespace math = nexenne::math;

namespace {
auto unit_about_z(double angle) -> math::quaternion_d {
  return *math::from_axis_angle(math::vector3_d{0, 0, 1}, math::radians_d{angle});
}
}  // namespace

TEST_CASE("nlerp_plain hits endpoints and stays unit length") {
  auto const a{unit_about_z(0.0)};
  auto const b{unit_about_z(math::half_pi)};
  CHECK(math::dot(math::nlerp_plain(a, b, 0.0), a) == doctest::Approx(1.0));
  CHECK(math::dot(math::nlerp_plain(a, b, 1.0), b) == doctest::Approx(1.0));
  CHECK(math::length(math::nlerp_plain(a, b, 0.5)) == doctest::Approx(1.0));
}

TEST_CASE("nlerp_short takes the shorter arc") {
  auto const a{unit_about_z(0.0)};
  // -a represents the same orientation but is antipodal; nlerp_short must flip it
  // so the result stays near a, not at the far end.
  auto const b{-a};
  auto const mid{math::nlerp_short(a, b, 0.5)};
  CHECK(math::length(mid) == doctest::Approx(1.0));
  CHECK(std::abs(math::dot(mid, a)) == doctest::Approx(1.0));  // same orientation as a
}

TEST_CASE("slerp_short matches the canonical slerp and is unit") {
  auto const a{unit_about_z(0.2)};
  auto const b{unit_about_z(1.3)};
  for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    auto const v{math::slerp_short(a, b, t)};
    auto const ref{math::slerp(a, b, t)};
    CHECK(math::length(v) == doctest::Approx(1.0));
    // Same orientation as the reference slerp (up to sign).
    CHECK(std::abs(math::dot(v, ref)) == doctest::Approx(1.0));
  }
}

TEST_CASE("slerp_short falls back to nlerp for nearly aligned inputs") {
  auto const a{unit_about_z(0.10)};
  auto const b{unit_about_z(0.10001)};  // angle far below the 0.9995 threshold gap
  auto const v{math::slerp_short(a, b, 0.5)};
  CHECK(math::length(v) == doctest::Approx(1.0));
}
