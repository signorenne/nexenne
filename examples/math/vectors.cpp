/**
 * @file
 * @brief Fixed-size vectors: algebra, the dot/cross products, normalization, and
 *        the unit-length wrapper.
 *
 *   1. vector{1.0f, 2.0f, 3.0f} deduces vector<float, 3>; the element-wise
 *      operators compile to packed SSE.
 *   2. Cross and dot products and the length.
 *   3. normalize returns a result, so the zero-length case is handled.
 *   4. reflect about a known unit normal (the y axis).
 *   5. The normalized wrapper carries the unit-length guarantee in the type.
 *   6. angle_between returns the angle between two vectors in radians.
 */

#include <print>

#include <nexenne/math/format.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  constexpr auto a{nm::vector{1.0f, 2.0f, 3.0f}};
  constexpr nm::vector3_f b{4, 5, 6};

  std::println("{:<22} {}", "a + b", a + b);
  std::println("{:<22} {}", "2 * a", 2.0f * a);
  std::println("{:<22} {}", "a x b (cross)", nm::cross(a, b));
  std::println("a . b (dot)            {}", nm::dot(a, b));
  std::println("length(a)             {:.4f}", nm::length(a));

  if (auto const u = nm::normalize(b)) {
    std::println("{:<22} {}", "normalize(b)", *u);
    std::println("  length now           {:.6f}", nm::length(*u));
  }

  constexpr nm::vector3_f normal{0, 1, 0};
  std::println("{:<22} {}", "reflect((1,-1,0), y)", nm::reflect(nm::vector3_f{1, -1, 0}, normal));

  if (auto const dir = nm::make_normalized(nm::vector3_f{1, 1, 1})) {
    std::println("{:<22} {}", "make_normalized(1,1,1)", dir->value());
  }

  if (auto const ang = nm::angle_between(a, b)) {
    std::println("angle_between(a, b)    {:.4f} rad", *ang);
  }
  return 0;
}
