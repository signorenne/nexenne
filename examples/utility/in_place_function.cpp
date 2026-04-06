/**
 * @file
 * @brief Store a capturing callable without the heap, via in_place_function.
 */

#include <print>
#include <utility>

#include <nexenne/utility/in_place_function.hpp>

namespace {

// A heap-free, move-only callback type: a discount rule the cart stores and
// applies later. No std::function, no allocation.
using discount = nexenne::utility::in_place_function<int(int), 32>;

}  // namespace

auto main() -> int {
  int const member_off{15};

  // Store a capturing lambda inline; the capture lives in the object itself.
  auto rule{discount{[](int price) { return price - member_off; }}};

  std::println("has rule: {}", static_cast<bool>(rule));
  std::println("100 -> {}", rule(100));
  std::println("250 -> {}", rule(250));

  // Move-only: ownership transfers, leaving the source empty.
  auto moved{std::move(rule)};
  std::println("moved-from empty: {}", !static_cast<bool>(rule));
  std::println("moved 80 -> {}", moved(80));

  return 0;
}
