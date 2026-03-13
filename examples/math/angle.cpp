/**
 * @file
 * @brief Strong angle types: typed conversions, algebra, and wrapping.
 *
 * The unit mismatch on the commented line below is a compile error, which is the
 * whole point of the strong types.
 */

#include <print>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  constexpr nm::degrees_d turn{90.0};
  constexpr auto r{nm::to_radians(turn)};
  std::println("90 deg = {} rad", r.value());
  std::println("sin(90 deg) = {}", nm::sin(r));

  // Algebra stays in the unit; there is no radians + degrees overload.
  constexpr auto sum{nm::degrees_d{30.0} + nm::degrees_d{15.0}};
  std::println("30 deg + 15 deg = {} deg", sum.value());
  // auto bad = nm::radians_d{1.0} + nm::degrees_d{1.0};  // compile error

  // Wrapping keeps a drifting heading bounded.
  std::println(
    "wrap_signed(7 rad)    = {} (in [-pi, pi))", nm::wrap_signed(nm::radians_d{7.0}).value()
  );
  std::println(
    "wrap_unsigned(-1 rad) = {} (in [0, 2pi))", nm::wrap_unsigned(nm::radians_d{-1.0}).value()
  );
  return 0;
}
