/**
 * @file
 * @brief Q-format fixed-point: integer-ALU fractional math with exact results.
 */

#include <print>

#include <nexenne/math/fixed.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  constexpr nm::q16_16 a{1.5};
  constexpr nm::q16_16 b{2};

  std::println("a = {} (raw {})", a.to_float(), a.raw());
  std::println("b = {} (raw {})", b.to_float(), b.raw());
  std::println("a + b = {}", (a + b).to_float());
  std::println("a * b = {}", (a * b).to_float());  // exact via wide intermediate
  std::println("a / b = {}", (a / b).to_float());
  std::println("(a + b).to_int() = {} (floor of 3.5)", (a + b).to_int());

  // Accumulate on the integer ALU, no FPU needed.
  nm::q16_16 acc{0};
  for (int i{0}; i < 4; ++i) {
    acc += nm::q16_16{0.25};
  }
  std::println("0.25 added four times = {}", acc.to_float());
  return 0;
}
