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

// A heap-free, move-only callback type: a discount rule the cart stores and
// applies later. No std::function, no allocation; the callable lives inside
// the object's 32 bytes of inline storage.
using discount = nexenne::utility::in_place_function<int(int), 32>;

// The capacity is a compile-time contract: a capture set that does not fit
// is a compile error at the construction site, never a silent heap fallback.
static_assert(discount::capacity == 32);

}  // namespace

auto main(int argc, char**) -> int {
  // Derive the offset at runtime (argc is 1 for a normal launch) so the lambda
  // genuinely captures member_off by value instead of folding a constant; that
  // captured copy lives in the object's inline storage and stays valid after
  // member_off's scope would end.
  int const member_off{15 * argc};

  auto rule{discount{[member_off](int price) { return price - member_off; }}};

  std::println("has rule: {}", static_cast<bool>(rule));
  std::println("100 -> {}", rule(100));
  std::println("250 -> {}", rule(250));

  // Move-only semantics: ownership of the stored callable transfers by
  // relocating it into the destination buffer, leaving the source empty.
  auto moved{std::move(rule)};
  std::println("moved-from empty: {}", !static_cast<bool>(rule));
  std::println("moved 80 -> {}", moved(80));

  // Reassignment destroys the old callable and stores the new one in place.
  moved = [](int price) { return price / 2; };
  std::println("half 80 -> {}", moved(80));

  // reset returns to the empty state; calling an empty instance asserts in
  // debug builds, so check operator bool first when emptiness is possible.
  moved.reset();
  std::println("after reset empty: {}", !static_cast<bool>(moved));

  return 0;
}
