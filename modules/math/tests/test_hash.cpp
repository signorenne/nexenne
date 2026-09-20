#include <doctest/doctest.h>

#include <unordered_map>
#include <unordered_set>

#include <nexenne/math/hash.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace math = nexenne::math;

TEST_CASE("equal values hash equally; the types work as hash keys") {
  math::vector3_d const a{1, 2, 3};
  math::vector3_d const b{1, 2, 3};
  std::hash<math::vector3_d> const h{};
  CHECK(h(a) == h(b));

  std::unordered_set<math::vector3_d> set;
  set.insert(a);
  set.insert(b);
  set.insert(math::vector3_d{1, 2, 4});
  CHECK(set.size() == 2);

  std::unordered_map<math::vector2_i, int> grid;
  grid[math::vector2_i{3, 4}] = 7;
  CHECK(grid.at(math::vector2_i{3, 4}) == 7);
}

TEST_CASE("plus and minus zero components hash equally") {
  // std::hash special-cases zero, so +0.0 and -0.0 (which compare equal) hash equally.
  std::hash<math::vector2_f> const h{};
  CHECK(h(math::vector2_f{0.0f, 0.0f}) == h(math::vector2_f{-0.0f, -0.0f}));
  CHECK(math::vector2_f{0.0f, 0.0f} == math::vector2_f{-0.0f, -0.0f});

  std::unordered_set<math::vector2_f> set;
  set.insert(math::vector2_f{0.0f, 0.0f});
  set.insert(math::vector2_f{-0.0f, -0.0f});
  CHECK(set.size() == 1);
}

TEST_CASE("hash is order-sensitive across components") {
  std::hash<math::vector3_d> const h{};
  CHECK(h(math::vector3_d{1, 2, 3}) != h(math::vector3_d{3, 2, 1}));
}

TEST_CASE("quaternion and matrix are hashable") {
  std::hash<math::quaternion_d> const hq{};
  CHECK(hq(math::quaternion_d{1, 2, 3, 4}) == hq(math::quaternion_d{1, 2, 3, 4}));
  CHECK(hq(math::quaternion_d{1, 2, 3, 4}) != hq(math::quaternion_d{4, 3, 2, 1}));

  std::hash<math::matrix3_d> const hm{};
  CHECK(hm(math::matrix3_d::identity()) == hm(math::matrix3_d::identity()));
  CHECK(hm(math::matrix3_d::identity()) != hm(math::matrix3_d{}));

  std::unordered_set<math::quaternion_d> qs;
  qs.insert(math::quaternion_d::identity());
  qs.insert(math::quaternion_d::identity());
  CHECK(qs.size() == 1);
}
