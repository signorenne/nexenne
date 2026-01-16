/**
 * @file
 * @brief bitset_dynamic as a dense set of integer indices.
 *
 * A runtime-sized bit per entity tracks which are alive, with word-level
 * popcount, a sparse scan over the live indices, and a forward scan-for-set.
 */

#include <cstddef>
#include <print>

#include <nexenne/container/bitset_dynamic.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::bitset_dynamic alive(100);  // 100 entities, none alive yet
  for (int const id : {3, 17, 42, 63, 64, 99}) {
    alive.set(static_cast<std::size_t>(id));
  }

  std::println("alive: {} of {}", alive.count(), alive.size());

  std::print("ids:");
  for (std::size_t const id : alive.set_bits()) {  // sparse: visits only live ids
    std::print(" {}", id);
  }
  std::println("");

  std::println("first alive at or after 50: {}", alive.find_first_set(50));
  // alive: 6 of 100
  // ids: 3 17 42 63 64 99
  // first alive at or after 50: 63
  return 0;
}
