/**
 * @file
 * @brief Column-major matrices: construction, multiply, inverse, and a
 *        perspective projection.
 */

#include <print>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/format.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/projection.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  // Written in reading order; stored column-major.
  constexpr auto a{nm::make_matrix2(1.0f, 2.0f, 3.0f, 4.0f)};
  constexpr auto b{nm::make_matrix2(5.0f, 6.0f, 7.0f, 8.0f)};
  std::println("{:<18} {}", "a", a);
  std::println("{:<18} {}", "a * b (packed SSE)", a * b);
  std::println("{:<18} {}", "a + b", a + b);
  std::println("{:<18} {}", "transpose(a)", nm::transpose(a));
  std::println("det(a) = {}", nm::determinant(a));

  if (auto const inv = nm::inverse(a)) {
    std::println("{:<18} {}", "inverse(a)", *inv);
    std::println("{:<18} {}", "a * inverse(a)", a * *inv);  // ~ identity
  }

  // data() is a column-major upload pointer: column 0 then column 1.
  std::println("a.data() = [{}, {}, {}, {}]", a.data()[0], a.data()[1], a.data()[2], a.data()[3]);

  // A perspective projection (60 deg vertical FOV, 16:9).
  auto const proj{nm::perspective(nm::to_radians(60.0).value(), 16.0 / 9.0, 0.1, 100.0)};
  std::println("");
  std::println(
    "perspective m(0,0)={:.4f} m(1,1)={:.4f} m(3,2)={}", proj(0, 0), proj(1, 1), proj(3, 2)
  );
  return 0;
}
