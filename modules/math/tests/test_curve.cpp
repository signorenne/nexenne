#include <doctest/doctest.h>

#include <cmath>

#include <nexenne/math/curve.hpp>
#include <nexenne/math/vector.hpp>

namespace math = nexenne::math;

namespace {
auto vapprox(math::vector2_d const& a, math::vector2_d const& b, double eps = 1e-9) -> bool {
  return std::abs(a.x() - b.x()) < eps && std::abs(a.y() - b.y()) < eps;
}
}  // namespace

TEST_CASE("Bezier curves interpolate their endpoints") {
  math::vector2_d const p0{0, 0};
  math::vector2_d const p1{0, 1};
  math::vector2_d const p2{1, 1};
  math::vector2_d const p3{1, 0};

  CHECK(vapprox(math::bezier_quadratic(p0, p1, p2, 0.0), p0));
  CHECK(vapprox(math::bezier_quadratic(p0, p1, p2, 1.0), p2));
  // Midpoint of the quadratic: 0.25*p0 + 0.5*p1 + 0.25*p2.
  CHECK(vapprox(math::bezier_quadratic(p0, p1, p2, 0.5), math::vector2_d{0.25, 0.75}));

  CHECK(vapprox(math::bezier_cubic(p0, p1, p2, p3, 0.0), p0));
  CHECK(vapprox(math::bezier_cubic(p0, p1, p2, p3, 1.0), p3));
  // Symmetric control net: the cubic midpoint is (0.5, 0.75).
  CHECK(vapprox(math::bezier_cubic(p0, p1, p2, p3, 0.5), math::vector2_d{0.5, 0.75}));
}

TEST_CASE("Catmull-Rom passes through the inner control points") {
  math::vector2_d const p0{-1, 0};
  math::vector2_d const p1{0, 0};
  math::vector2_d const p2{1, 1};
  math::vector2_d const p3{2, 1};
  CHECK(vapprox(math::catmull_rom(p0, p1, p2, p3, 0.0), p1));
  CHECK(vapprox(math::catmull_rom(p0, p1, p2, p3, 1.0), p2));
  // Interior oracle, derived by hand from the uniform Catmull-Rom basis at
  // t=0.5: 0.5*(2*p1 + (p2-p0)*0.5 + (2p0-5p1+4p2-p3)*0.25 + (-p0+3p1-3p2+p3)*0.125)
  // = 0.5*((0,0) + (1,0.5) + (0,0.75) + (0,-0.25)) = (0.5, 0.5).
  CHECK(vapprox(math::catmull_rom(p0, p1, p2, p3, 0.5), math::vector2_d{0.5, 0.5}));
}

TEST_CASE("Hermite interpolates endpoints and matches the endpoint tangents") {
  math::vector2_d const p0{0, 0};
  math::vector2_d const p1{1, 0};
  math::vector2_d const m0{1, 2};
  math::vector2_d const m1{1, -2};
  CHECK(vapprox(math::hermite(p0, m0, p1, m1, 0.0), p0));
  CHECK(vapprox(math::hermite(p0, m0, p1, m1, 1.0), p1));
  // The analytic tangent equals the prescribed tangents at the ends.
  CHECK(vapprox(math::hermite_tangent(p0, m0, p1, m1, 0.0), m0));
  CHECK(vapprox(math::hermite_tangent(p0, m0, p1, m1, 1.0), m1));
}

TEST_CASE("every tangent matches a central finite difference of its curve") {
  math::vector2_d const a{0.2, -0.5};
  math::vector2_d const b{1.0, 2.0};
  math::vector2_d const c{-0.3, 0.7};
  math::vector2_d const d{1.5, -1.0};
  double const h{1e-6};
  double const t{0.37};
  auto fd = [&](auto&& f) { return (f(t + h) - f(t - h)) * (1.0 / (2.0 * h)); };
  CHECK(vapprox(
    math::bezier_quadratic_tangent(a, b, c, t),
    fd([&](double u) { return math::bezier_quadratic(a, b, c, u); }),
    1e-5
  ));
  CHECK(vapprox(
    math::bezier_cubic_tangent(a, b, c, d, t),
    fd([&](double u) { return math::bezier_cubic(a, b, c, d, u); }),
    1e-5
  ));
  CHECK(vapprox(
    math::catmull_rom_tangent(a, b, c, d, t),
    fd([&](double u) { return math::catmull_rom(a, b, c, d, u); }),
    1e-5
  ));
  CHECK(vapprox(
    math::hermite_tangent(a, b, c, d, t),
    fd([&](double u) { return math::hermite(a, b, c, d, u); }),
    1e-5
  ));
}

TEST_CASE("easing curves are clamped, pinned at the ends, and symmetric at the middle") {
  static_assert(math::ease_smoothstep(0.0) == 0.0);
  static_assert(math::ease_smoothstep(1.0) == 1.0);
  static_assert(math::ease_smootherstep(0.0) == 0.0);
  static_assert(math::ease_smootherstep(1.0) == 1.0);
  CHECK(math::ease_smoothstep(0.5) == doctest::Approx(0.5));
  CHECK(math::ease_smootherstep(0.5) == doctest::Approx(0.5));
  // Out-of-range input is clamped, not extrapolated.
  CHECK(math::ease_smoothstep(-1.0) == doctest::Approx(0.0));
  CHECK(math::ease_smootherstep(2.0) == doctest::Approx(1.0));
}

TEST_CASE("curves work on a scalar point too (easing a single value)") {
  // The affine_point concept admits the scalar parameter type itself.
  static_assert(math::affine_point<double, double>);
  CHECK(math::bezier_quadratic(0.0, 10.0, 0.0, 0.5) == doctest::Approx(5.0));
  CHECK(math::hermite(0.0, 0.0, 1.0, 0.0, 0.5) == doctest::Approx(0.5));
}
