/**
 * @file
 * @brief Store a capturing callable without the heap, via in_place_function.
 *
 * A cart keeps a discount rule as a heap-free, move-only callback whose
 * callable lives inside 32 bytes of inline storage. The capacity is a
 * compile-time contract: a capture set that does not fit is a compile error at
 * the construction site, never a silent heap fallback. The example:
 *
 *   1. stores a lambda capturing an offset derived from \c argc (1 on a normal
 *      launch), so the capture is a real runtime copy held in the inline
 *      storage rather than a folded constant;
 *   2. moves the callback, which relocates the callable and leaves the source
 *      empty;
 *   3. reassigns it, destroying the old callable and storing the new one in
 *      place;
 *   4. resets it to empty; calling an empty instance asserts in debug builds,
 *      so check \c operator \c bool first when emptiness is possible.
 */

#include <print>
#include <utility>

#include <nexenne/utility/in_place_function.hpp>

namespace {

using discount = nexenne::utility::in_place_function<int(int), 32>;

static_assert(discount::capacity == 32);

}  // namespace

auto main(int argc, char**) -> int {
  int const member_off{15 * argc};

  auto rule{discount{[member_off](int price) { return price - member_off; }}};

  std::println("has rule: {}", static_cast<bool>(rule));
  std::println("100 -> {}", rule(100));
  std::println("250 -> {}", rule(250));

  auto moved{std::move(rule)};
  std::println("moved-from empty: {}", !static_cast<bool>(rule));
  std::println("moved 80 -> {}", moved(80));

  moved = [](int price) { return price / 2; };
  std::println("half 80 -> {}", moved(80));

  moved.reset();
  std::println("after reset empty: {}", !static_cast<bool>(moved));

  return 0;
}
