#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/math/format.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

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
