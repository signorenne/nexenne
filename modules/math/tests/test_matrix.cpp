#include <doctest/doctest.h>

#include <type_traits>

#include <nexenne/math/matrix.hpp>

namespace math = nexenne::math;

TEST_CASE("layout guarantees (column-major, contiguous, no padding)") {
  static_assert(std::is_standard_layout_v<math::matrix4_f>);
  static_assert(std::is_trivially_copyable_v<math::matrix4_f>);
  static_assert(sizeof(math::matrix4_f) == 16 * sizeof(float));
  static_assert(sizeof(math::matrix3_d) == 9 * sizeof(double));
  static_assert(std::is_same_v<math::matrix4_f::column_type, math::vector4_f>);
}

TEST_CASE("row-major factory stores column-major, accessors agree") {
  constexpr auto m{math::make_matrix2(
    1.0f,
    2.0f,  // row 0
    3.0f,
    4.0f
  )};  // row 1
  static_assert(m(0, 0) == 1.0f);
  static_assert(m(0, 1) == 2.0f);
  static_assert(m(1, 0) == 3.0f);
  static_assert(m(1, 1) == 4.0f);
  // Column-major: column 0 is (1, 3), and data() lays it out that way.
  static_assert(m[0] == math::vector2_f{1, 3});
  static_assert(m.data()[0] == 1.0f);
  static_assert(m.data()[1] == 3.0f);  // next contiguous scalar is row 1 of col 0
  static_assert(m.row(0) == math::vector2_f{1, 2});
}

TEST_CASE("identity, zero, diagonal, trace, transpose") {
  constexpr auto id{math::matrix3_f::identity()};
  static_assert(id(0, 0) == 1.0f && id(1, 1) == 1.0f && id(2, 2) == 1.0f);
  static_assert(id(0, 1) == 0.0f);
  static_assert(math::trace(id) == 3.0f);
  static_assert(math::matrix3_f::diagonal(5.0f)(1, 1) == 5.0f);

  constexpr auto m{math::make_matrix2(1.0f, 2.0f, 3.0f, 4.0f)};
  static_assert(math::transpose(m) == math::make_matrix2(1.0f, 3.0f, 2.0f, 4.0f));
}

TEST_CASE("element-wise algebra and scalar scaling") {
  constexpr auto a{math::make_matrix2(1.0f, 2.0f, 3.0f, 4.0f)};
  constexpr auto b{math::make_matrix2(5.0f, 6.0f, 7.0f, 8.0f)};
  static_assert((a + b) == math::make_matrix2(6.0f, 8.0f, 10.0f, 12.0f));
  static_assert((b - a) == math::make_matrix2(4.0f, 4.0f, 4.0f, 4.0f));
  static_assert((a * 2.0f) == math::make_matrix2(2.0f, 4.0f, 6.0f, 8.0f));
  static_assert((2.0f * a) == a * 2.0f);
}

TEST_CASE("matrix-matrix multiply (column-combination, SIMD form)") {
  constexpr auto a{math::make_matrix2(1.0f, 2.0f, 3.0f, 4.0f)};
  constexpr auto b{math::make_matrix2(5.0f, 6.0f, 7.0f, 8.0f)};
  // [1 2][5 6] = [19 22]
  // [3 4][7 8]   [43 50]
  static_assert((a * b) == math::make_matrix2(19.0f, 22.0f, 43.0f, 50.0f));
  // Identity is the multiplicative identity.
  static_assert((a * math::matrix2_f::identity()) == a);
  static_assert((math::matrix2_f::identity() * a) == a);

  // 3x3 against a hand-checked product.
  constexpr auto id3{math::matrix3_f::identity()};
  constexpr auto t{math::make_matrix3(1.0f, 0.0f, 5.0f, 0.0f, 1.0f, 6.0f, 0.0f, 0.0f, 1.0f)};
  static_assert((t * id3) == t);
}

TEST_CASE("matrix-vector multiply transforms a column vector") {
  // A 3x3 with a translation column applied to a homogeneous-ish point.
  constexpr auto m{math::make_matrix3(2.0f, 0.0f, 10.0f, 0.0f, 3.0f, 20.0f, 0.0f, 0.0f, 1.0f)};
  constexpr math::vector3_f p{4, 5, 1};
  // row0: 2*4 + 0 + 10*1 = 18 ; row1: 0 + 3*5 + 20*1 = 35 ; row2: 1
  static_assert((m * p) == math::vector3_f{18, 35, 1});
}

TEST_CASE("determinant for 2x2, 3x3, 4x4") {
  static_assert(math::determinant(math::make_matrix2(1.0f, 2.0f, 3.0f, 4.0f)) == -2.0f);
  static_assert(math::determinant(math::matrix3_f::identity()) == 1.0f);
  static_assert(math::determinant(math::matrix4_f::identity()) == 1.0f);
  static_assert(math::determinant(math::matrix3_f::diagonal(2.0f)) == 8.0f);
}

TEST_CASE("inverse round-trips to identity, rejects singular") {
  auto const m{math::make_matrix2(4.0, 7.0, 2.0, 6.0)};
  auto const inv{math::inverse(m)};
  REQUIRE(inv.has_value());
  auto const prod{m * *inv};
  CHECK(prod(0, 0) == doctest::Approx(1.0));
  CHECK(prod(1, 1) == doctest::Approx(1.0));
  CHECK(prod(0, 1) == doctest::Approx(0.0));

  // 4x4 round-trip.
  auto const m4{math::make_matrix4(
    1.0, 2.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0
  )};
  auto const inv4{math::inverse(m4)};
  REQUIRE(inv4.has_value());
  auto const prod4{m4 * *inv4};
  for (std::size_t i{0}; i < 4; ++i) {
    CHECK(prod4(i, i) == doctest::Approx(1.0));
  }

  // Singular matrix (zero determinant) returns an error.
  auto const singular{math::inverse(math::make_matrix2(1.0, 2.0, 2.0, 4.0))};
  REQUIRE_FALSE(singular.has_value());
  CHECK(singular.error() == math::math_error::singular_matrix);
}
