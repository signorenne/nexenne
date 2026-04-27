/**
 * @file
 * @brief Pass any callable without templating, via nexenne::utility::function_ref.
 */

#include <array>
#include <cstddef>
#include <print>
#include <span>

#include <nexenne/utility/function_ref.hpp>

namespace {

// Accepts "any predicate" without becoming a template: one instantiation,
// any call site, and the implementation could live in a .cpp file. The view
// is two pointers, so taking it by value is the idiomatic parameter style.
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

// A free function binds through the dedicated function-pointer constructor,
// which stores the pointer itself: no lifetime hazard at all.
auto is_odd(int x) -> bool {
  return x % 2 != 0;
}

}  // namespace

auto main() -> int {
  std::array<int const, 6> const values{4, 17, 2, 33, 8, 21};

  // Lifetime rule, safe case 1: a named local lambda outlives the view that
  // count_if creates from it.
  auto const big{[](int x) { return x > 10; }};
  auto const over_ten{count_if(values, big)};

  // Lifetime rule, safe case 2: a temporary passed directly as the argument
  // lives for the whole call expression, so the view never outlives it.
  auto const even{count_if(values, [](int x) { return x % 2 == 0; })};

  // Free functions carry no lifetime risk (the pointer is stored by value).
  auto const odd{count_if(values, is_odd)};

  // What NOT to do: binding a temporary lambda to a named function_ref
  // dangles the moment the statement ends, so the type deletes assignment
  // from arbitrary callables and this guide names locals instead.
  //   nexenne::utility::function_ref<bool(int)> bad{[](int x) { return x > 0; }};  // dangles

  std::println("values over ten: {}", over_ten);
  std::println("even values: {}", even);
  std::println("odd values: {}", odd);
  return 0;
}
