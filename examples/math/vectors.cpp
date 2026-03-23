/**
 * @file
 * @brief Fixed-size vectors: algebra, the dot/cross products, normalization, and
 *        the unit-length wrapper.
 */

#include <print>

#include <nexenne/math/format.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  constexpr auto a{nm::vector{1.0f, 2.0f, 3.0f}};  // deduced vector<float,3>
  constexpr nm::vector3_f b{4, 5, 6};

  std::println("{:<22} {}", "a + b", a + b);  // compiles to packed SSE
  std::println("{:<22} {}", "2 * a", 2.0f * a);
  std::println("{:<22} {}", "a x b (cross)", nm::cross(a, b));
  std::println("a . b (dot)            {}", nm::dot(a, b));
  std::println("length(a)             {:.4f}", nm::length(a));

  // Normalization returns a result; handle the zero-length case.
  if (auto const u = nm::normalize(b)) {
    std::println("{:<22} {}", "normalize(b)", *u);
    std::println("  length now           {:.6f}", nm::length(*u));
  }

  // Reflection about a unit normal.
  constexpr nm::vector3_f normal{0, 1, 0};  // known unit (the y axis)
  std::println("{:<22} {}", "reflect((1,-1,0), y)", nm::reflect(nm::vector3_f{1, -1, 0}, normal));

  // The unit-length wrapper carries the guarantee in the type.
  if (auto const dir = nm::make_normalized(nm::vector3_f{1, 1, 1})) {
    std::println("{:<22} {}", "make_normalized(1,1,1)", dir->value());
  }

  // Angle between two vectors (radians).
  if (auto const ang = nm::angle_between(a, b)) {
    std::println("angle_between(a, b)    {:.4f} rad", *ang);
  }
  return 0;
}
