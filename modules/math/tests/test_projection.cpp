#include <doctest/doctest.h>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/projection.hpp>

namespace math = nexenne::math;

TEST_CASE("perspective maps the frustum corners into the clip cube") {
  auto const p{math::perspective(math::half_pi, 16.0 / 9.0, 0.1, 100.0)};
  // fovy = 90 deg => f = cot(45) = 1, so m(1,1) == 1 and m(0,0) == 1/aspect.
  CHECK(p(1, 1) == doctest::Approx(1.0));
  CHECK(p(0, 0) == doctest::Approx(9.0 / 16.0));
  // The -1 in (3,2) copies -z into clip w (the perspective divide).
  CHECK(p(3, 2) == doctest::Approx(-1.0));

  // A point on the near plane maps to clip z = -near*... ; check the depth maps
  // monotonically: near maps to -1, far maps to +1 after the w divide.
  auto const near_pt{p * math::vector4_d{0, 0, -0.1, 1}};
  auto const far_pt{p * math::vector4_d{0, 0, -100.0, 1}};
  CHECK(near_pt.z() / near_pt.w() == doctest::Approx(-1.0).epsilon(1e-6));
  CHECK(far_pt.z() / far_pt.w() == doctest::Approx(1.0).epsilon(1e-6));
}

TEST_CASE("perspective_zo maps depth to [0, 1]") {
  auto const p{math::perspective_zo(math::half_pi, 1.0, 0.1, 100.0)};
  auto const near_pt{p * math::vector4_d{0, 0, -0.1, 1}};
  auto const far_pt{p * math::vector4_d{0, 0, -100.0, 1}};
  CHECK(near_pt.z() / near_pt.w() == doctest::Approx(0.0).epsilon(1e-6));
  CHECK(far_pt.z() / far_pt.w() == doctest::Approx(1.0).epsilon(1e-6));
}

TEST_CASE("ortho maps the box edges onto the clip cube") {
  auto const o{math::ortho(-2.0, 2.0, -1.0, 1.0, 0.1, 100.0)};
  // Center of the box maps to the origin in x and y.
  auto const center{o * math::vector4_d{0, 0, -50.0, 1}};
  CHECK(center.x() == doctest::Approx(0.0));
  CHECK(center.y() == doctest::Approx(0.0));
  // Right edge x = 2 maps to clip x = +1.
  auto const right{o * math::vector4_d{2, 0, -50.0, 1}};
  CHECK(right.x() == doctest::Approx(1.0));
  // ortho has no perspective divide: w stays 1.
  CHECK(right.w() == doctest::Approx(1.0));
}

TEST_CASE("ortho_zo maps depth to [0, 1]") {
  auto const o{math::ortho_zo(-1.0, 1.0, -1.0, 1.0, 0.0, 10.0)};
  auto const near_pt{o * math::vector4_d{0, 0, 0.0, 1}};
  auto const far_pt{o * math::vector4_d{0, 0, -10.0, 1}};
  CHECK(near_pt.z() == doctest::Approx(0.0));
  CHECK(far_pt.z() == doctest::Approx(1.0));
}
