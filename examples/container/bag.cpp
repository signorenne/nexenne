/**
 * @file
 * @brief bag as an active-entity list: add, remove by swap-pop, iterate.
 *
 * Order does not matter for a "what is active this frame" set, so removal is an
 * O(1) swap-pop rather than an order-preserving shift.
 */

#include <print>

#include <nexenne/container/bag.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::bag<int> active;  // ids of active entities; order is irrelevant
  for (int const id : {10, 20, 30, 40, 50}) {
    active.insert(id);
  }
  std::println("active: {}", active.size());

  active.erase_first(30);  // entity 30 died: O(1) swap-pop
  active.erase_all(50);    // remove every 50

  std::print("remaining:");
  for (int const id : active) {
    std::print(" {}", id);
  }
  std::println("");
  // active: 5
  // remaining: 10 20 40
  return 0;
}
