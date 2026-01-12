/**
 * @file
 * @brief Introspect a callable's signature to drive a generic adapter.
 *
 * Recovers a function's return type, arity, and argument types at compile time,
 * and rejects unsupported argument types with a readable diagnostic via
 * always_false_v.
 */

#include <print>
#include <string_view>
#include <type_traits>

#include <nexenne/utility/meta.hpp>

namespace util = nexenne::utility;

auto compute(int a, double b) noexcept -> double {
  return a * b;
}

// A compile-time category label, exercising the always_false_v idiom.
template <typename T>
constexpr auto category() noexcept -> std::string_view {
  if constexpr (std::is_integral_v<T>) {
    return "integral";
  } else if constexpr (std::is_floating_point_v<T>) {
    return "floating";
  } else {
    static_assert(util::always_false_v<T>, "category: unsupported type");
  }
}

auto main() -> int {
  using fn = decltype(&compute);
  static_assert(util::function_arity_v<fn> == 2, "two parameters");
  static_assert(std::is_same_v<util::function_return_t<fn>, double>);
  static_assert(std::is_same_v<util::function_arg_t<fn, 0>, int>);
  static_assert(std::is_same_v<util::function_arg_t<fn, 1>, double>);
  static_assert(util::function_is_noexcept_v<fn>, "compute is noexcept");

  static_assert(category<int>() == "integral");
  static_assert(category<double>() == "floating");

  std::println("compute arity: {}", util::function_arity_v<fn>);
  std::println("compute is noexcept: {}", util::function_is_noexcept_v<fn>);
  std::println("return type spelled: {}", util::type_name<util::function_return_t<fn>>());
  std::println("arg 0 is {}", category<util::function_arg_t<fn, 0>>());
  return 0;
}
