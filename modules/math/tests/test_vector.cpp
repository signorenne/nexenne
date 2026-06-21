#include <doctest/doctest.h>

#include <array>
#include <type_traits>

#include <nexenne/math/vector.hpp>

namespace math = nexenne::math;

TEST_CASE("layout guarantees hold (SIMD-friendly storage)") {
  static_assert(std::is_standard_layout_v<math::vector4_f>);
  static_assert(std::is_trivially_copyable_v<math::vector4_f>);
  static_assert(sizeof(math::vector3_f) == 3 * sizeof(float));
  static_assert(sizeof(math::vector4_d) == 4 * sizeof(double));
  static_assert(math::vector4_f::value_type{} == 0.0f);
}

TEST_CASE("construction, deduction guides, and named accessors") {
  constexpr auto a{math::vector{1.0f, 2.0f, 3.0f}};
  static_assert(std::is_same_v<decltype(a), math::vector<float, 3> const>);
  static_assert(a.x() == 1.0f);
  static_assert(a.y() == 2.0f);
  static_assert(a.z() == 3.0f);
  static_assert(a.size() == 3);

  constexpr math::vector4_f b{1, 2, 3, 4};
  static_assert(b.w() == 4.0f);
  static_assert(b[2] == 3.0f);

  // From an array.
  constexpr math::vector<float, 3> c{std::array<float, 3>{5, 6, 7}};
  static_assert(c.x() == 5.0f);
}

TEST_CASE("element-wise algebra") {
  constexpr math::vector3_f a{1, 2, 3};
  constexpr math::vector3_f b{4, 5, 6};
  static_assert((a + b) == math::vector3_f{5, 7, 9});
  static_assert((b - a) == math::vector3_f{3, 3, 3});
  static_assert((a * b) == math::vector3_f{4, 10, 18});  // Hadamard
  static_assert((b / a) == math::vector3_f{4, 2.5f, 2});
  static_assert((-a) == math::vector3_f{-1, -2, -3});
}

TEST_CASE("scalar algebra on both sides") {
  constexpr math::vector3_f a{1, 2, 3};
  static_assert((a * 2.0f) == math::vector3_f{2, 4, 6});
  static_assert((2.0f * a) == math::vector3_f{2, 4, 6});
  static_assert((a / 2.0f) == math::vector3_f{0.5f, 1, 1.5f});
}

TEST_CASE("compound assignment") {
  math::vector3_f a{1, 2, 3};
  a += math::vector3_f{1, 1, 1};
  CHECK(a == math::vector3_f{2, 3, 4});
  a -= math::vector3_f{1, 1, 1};
  CHECK(a == math::vector3_f{1, 2, 3});
  a *= 2.0f;
  CHECK(a == math::vector3_f{2, 4, 6});
  a /= 2.0f;
  CHECK(a == math::vector3_f{1, 2, 3});
  a *= math::vector3_f{2, 2, 2};
  CHECK(a == math::vector3_f{2, 4, 6});
}

TEST_CASE("comparison sees every component through the base") {
  static_assert(math::vector4_f{1, 2, 3, 4} == math::vector4_f{1, 2, 3, 4});
  static_assert(math::vector4_f{1, 2, 3, 4} != math::vector4_f{1, 2, 3, 5});
  static_assert(math::vector3_f{1, 2, 3} < math::vector3_f{1, 2, 4});  // lexicographic
}

TEST_CASE("swizzles") {
  constexpr math::vector4_f v{1, 2, 3, 4};
  static_assert(v.xyz() == math::vector3_f{1, 2, 3});
  static_assert(v.xyw() == math::vector3_f{1, 2, 4});
  static_assert(v.xzw() == math::vector3_f{1, 3, 4});
  static_assert(v.yzw() == math::vector3_f{2, 3, 4});
  static_assert(v.xy() == math::vector2_f{1, 2});
  static_assert(v.xz() == math::vector2_f{1, 3});
  static_assert(v.yz() == math::vector2_f{2, 3});

  constexpr math::vector3_f w{7, 8, 9};
  static_assert(w.xy() == math::vector2_f{7, 8});
  static_assert(w.yz() == math::vector2_f{8, 9});
}

TEST_CASE("iteration and data pointer expose contiguous storage") {
  math::vector4_f v{1, 2, 3, 4};
  float sum{0};
  for (float const e : v) {
    sum += e;
  }
  CHECK(sum == 10.0f);
  CHECK(v.data()[0] == 1.0f);
  CHECK(v.data()[3] == 4.0f);
  *(v.data() + 1) = 20.0f;
  CHECK(v.y() == 20.0f);
}

TEST_CASE("works at arbitrary N via the primary template") {
  math::vector<float, 5> a{std::array<float, 5>{1, 2, 3, 4, 5}};
  math::vector<float, 5> b{std::array<float, 5>{5, 4, 3, 2, 1}};
  auto const c{a + b};
  CHECK(c[0] == 6.0f);
  CHECK(c[4] == 6.0f);
  CHECK(c.size() == 5);
}
