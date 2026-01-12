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
// any call site. The callable must outlive this call (it always does, since
// the argument is alive for the full expression).
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

}  // namespace

auto main() -> int {
  std::array<int const, 6> const values{4, 17, 2, 33, 8, 21};

  // A named local lambda: it outlives the function_ref bound inside count_if.
  auto const big{[](int x) { return x > 10; }};
  auto const over_ten{count_if(values, big)};

  // A temporary passed directly as the argument is also fine: it lives for
  // the whole call expression.
  auto const even{count_if(values, [](int x) { return x % 2 == 0; })};

  std::println("values over ten: {}", over_ten);
  std::println("even values: {}", even);
  return 0;
}
