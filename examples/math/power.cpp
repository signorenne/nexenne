/**
 * @file
 * @brief Roots, integer powers, and the fast bit-trick transcendentals.
 *
 * Each fast function is printed next to its libm reference so the accuracy
 * trade-off is visible.
 */

#include <cmath>
#include <print>

#include <nexenne/math/power.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  // Exact (to rounding), constexpr-capable.
  std::println("sqrt(2)        = {}", nm::sqrt(2.0));
  std::println("pow_int(2, 10) = {}", nm::pow_int(2.0, 10));

  std::println("");
  std::println("{:<14} {:>14} {:>14}", "x", "fast", "libm");
  for (double x : {0.5, 2.0, 9.0, 100.0}) {
    std::println("inv_sqrt {:<5} {:>14.8f} {:>14.8f}", x, nm::fast_inv_sqrt(x), 1.0 / std::sqrt(x));
  }
  for (double x : {-1.0, 0.0, 2.5}) {
    std::println("exp {:<10} {:>14.8f} {:>14.8f}", x, nm::fast_exp(x), std::exp(x));
  }
  for (double x : {0.5, 2.0, 100.0}) {
    std::println("log {:<10} {:>14.8f} {:>14.8f}", x, nm::fast_log(x), std::log(x));
  }
  return 0;
}
