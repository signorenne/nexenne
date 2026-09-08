/**
 * @file
 * @brief Pass any callable without templating, via nexenne::utility::function_ref.
 *
 * \c count_if takes "any predicate" as a \c function_ref instead of a template
 * parameter: one instantiation serves every call site, the implementation could
 * live in a .cpp file, and the view is two pointers, so it is taken by value.
 * The view does not own its callable, so the example walks the lifetime rule:
 *
 *   1. a named local lambda outlives the view \c count_if makes from it;
 *   2. a temporary lambda passed straight as the argument lives for the whole
 *      call expression, so the view never outlives it;
 *   3. a free function binds through the function-pointer constructor, which
 *      stores the pointer itself and carries no lifetime risk.
 *
 * What not to do: binding a temporary lambda to a named \c function_ref
 * dangles once the statement ends, which is why the type deletes assignment
 * from arbitrary callables and why the example names its locals.
 */

#include <array>
#include <cstddef>
#include <print>
#include <span>

#include <nexenne/utility/function_ref.hpp>

namespace {

auto count_if(std::span<int const> data, nexenne::utility::function_ref<bool(int)> pred)
  -> std::size_t {
  std::size_t n{0};
  for (auto const x : data) {
    if (pred(x)) {
      ++n;
    }
  }
  return n;
}

auto is_odd(int x) -> bool {
  return x % 2 != 0;
}

}  // namespace

auto main() -> int {
  std::array<int const, 6> const values{4, 17, 2, 33, 8, 21};

  auto const big{[](int x) { return x > 10; }};
  auto const over_ten{count_if(values, big)};

  auto const even{count_if(values, [](int x) { return x % 2 == 0; })};

  auto const odd{count_if(values, is_odd)};

  std::println("values over ten: {}", over_ten);
  std::println("even values: {}", even);
  std::println("odd values: {}", odd);
  return 0;
}
