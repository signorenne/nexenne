#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/error.hpp>
#include <nexenne/math/euler.hpp>
#include <nexenne/math/fixed.hpp>
#include <nexenne/math/format.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/trigonometry.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/utility/discard.hpp>

namespace math = nexenne::math;

TEST_CASE("to_string names the dimension without an underscore and lists components") {
  CHECK(math::to_string(math::vector2_i{1, 2}) == "vector2(1, 2)");
  CHECK(math::to_string(math::vector3_i{1, 2, 3}) == "vector3(1, 2, 3)");
  CHECK(math::to_string(math::vector4_i{1, 2, 3, 4}) == "vector4(1, 2, 3, 4)");
}

TEST_CASE("quaternion prints with a semicolon before the scalar part") {
  // quaternion{x, y, z, w}.
  CHECK(math::to_string(math::quaternion_d{1, 2, 3, 4}) == "quaternion(1, 2, 3; 4)");
}

TEST_CASE("matrix prints row-major despite column-major storage") {
  // Identity reads as rows; a non-symmetric matrix proves the row-major order.
  CHECK(math::to_string(math::matrix3_d::identity()) == "matrix3(1, 0, 0; 0, 1, 0; 0, 0, 1)");
  auto m{math::matrix2_d::identity()};
  m(0, 1) = 9;  // row 0, col 1
  CHECK(math::to_string(m) == "matrix2(1, 9; 0, 1)");
}

TEST_CASE("normalized wraps the underlying vector's string") {
  auto const n{math::make_normalized(math::vector3_d{0, 3, 4})};
  REQUIRE(n.has_value());
  CHECK(math::to_string(*n) == "normalized(vector3(0, 0.6, 0.8))");
}

TEST_CASE("std::format and operator<< agree with to_string") {
  math::vector3_i const v{5, 6, 7};
  CHECK(std::format("{}", v) == math::to_string(v));

  std::ostringstream os;
  os << v;
  CHECK(os.str() == math::to_string(v));

  // Works embedded in a larger format string.
  CHECK(std::format("v = {}!", v) == "v = vector3(5, 6, 7)!");
}

TEST_CASE("formatters forward the spec to each component") {
  // The format spec applies to every element, so precision/sign/width work.
  CHECK(std::format("{:+.2f}", math::vector3_d{1.5, -2.0, 3.0}) == "vector3(+1.50, -2.00, +3.00)");
  CHECK(std::format("{:.1f}", math::quaternion_d{1, 2, 3, 4}) == "quaternion(1.0, 2.0, 3.0; 4.0)");
  CHECK(std::format("{:.1f}", math::matrix2_d::identity()) == "matrix2(1.0, 0.0; 0.0, 1.0)");
  // The empty spec still gives the default rendering.
  CHECK(std::format("{}", math::vector3_i{5, 6, 7}) == "vector3(5, 6, 7)");

  // An invalid component spec is rejected by the component formatter.
  math::vector3_d const v{1, 2, 3};
  CHECK_THROWS_AS(
    nexenne::utility::discard(std::vformat("{:Z}", std::make_format_args(v))), std::format_error
  );
}

TEST_CASE("scalar-side value types print through all three layers (m1)") {
  // radians and degrees name their unit.
  CHECK(math::to_string(math::radians_d{1.5}) == "1.5 rad");
  CHECK(math::to_string(math::degrees_d{90.0}) == "90 deg");
  CHECK(std::format("{}", math::radians_d{1.5}) == "1.5 rad");
  CHECK(std::format("{:.2f}", math::radians_d{1.5}) == "1.50 rad");
  CHECK(std::format("{:.1f}", math::degrees_f{45.0f}) == "45.0 deg");

  // fixed states its Q-format and decimal value.
  CHECK(math::to_string(math::q16_16{1.5}) == "q16.16(1.5)");
  CHECK(math::to_string(math::q8_8{-2.0}) == "q8.8(-2)");
  CHECK(std::format("{}", math::q16_16{1.5}) == "q16.16(1.5)");
  CHECK(std::format("{:.3f}", math::q16_16{1.5}) == "q16.16(1.500)");

  // sin_cos lists sine then cosine.
  CHECK(math::to_string(math::sin_cos<double>{0.5, 0.75}) == "sin_cos(0.5, 0.75)");
  CHECK(std::format("{:.1f}", math::sin_cos<double>{0.5, 0.75}) == "sin_cos(0.5, 0.8)");

  // operator<< agrees with to_string.
  std::ostringstream os;
  os << math::radians_d{1.5} << ' ' << math::q16_16{1.5};
  CHECK(os.str() == "1.5 rad q16.16(1.5)");
}

TEST_CASE("math_error prints via all three layers (m1)") {
  CHECK(math::to_string(math::math_error::singular_matrix) == "singular_matrix");
  CHECK(std::format("{}", math::math_error::singular_matrix) == "singular_matrix");
  std::ostringstream os;
  os << math::math_error::zero_length_vector;
  CHECK(os.str() == "zero_length_vector");
}

TEST_CASE("axis_angle prints through all three layers (math-core routed)") {
  math::axis_angle<double> const a{math::vector3_d{1.0, 0.0, 0.0}, math::radians_d{1.5}};
  CHECK(math::to_string(a) == "axis_angle(axis=vector3(1, 0, 0), angle=1.5 rad)");
  CHECK(std::format("{}", a) == "axis_angle(axis=vector3(1, 0, 0), angle=1.5 rad)");
  // The spec applies to every scalar (three axis components and the angle).
  CHECK(std::format("{:.1f}", a) == "axis_angle(axis=vector3(1.0, 0.0, 0.0), angle=1.5 rad)");
  std::ostringstream os;
  os << a;
  CHECK(os.str() == math::to_string(a));
}

TEST_CASE("euler_angles prints through all three layers (math-core routed)") {
  math::euler_angles<double> const e{math::radians{0.5}, math::radians{1.0}, math::radians{-0.25}};
  CHECK(math::to_string(e) == "euler_angles(x=0.5, y=1, z=-0.25 rad)");
  CHECK(std::format("{}", e) == "euler_angles(x=0.5, y=1, z=-0.25 rad)");
  CHECK(std::format("{:.2f}", e) == "euler_angles(x=0.50, y=1.00, z=-0.25 rad)");
  std::ostringstream os;
  os << e;
  CHECK(os.str() == math::to_string(e));
}

TEST_CASE("euler_order enum prints its lowercase name (math-core routed)") {
  CHECK(math::to_string(math::euler_order::xyz) == "xyz");
  CHECK(math::to_string(math::euler_order::zyx) == "zyx");
  CHECK(std::format("{}", math::euler_order::yzx) == "yzx");
  // Inherits the string formatter, so width/alignment specs work.
  CHECK(std::format("{:>5}", math::euler_order::xzy) == "  xzy");
  std::ostringstream os;
  os << math::euler_order::yxz;
  CHECK(os.str() == "yxz");
  static_assert(math::to_string(math::euler_order::zxy) == "zxy");
}
