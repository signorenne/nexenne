/**
 * @file
 * @brief sparse_set as an ECS tag: O(1) membership over entity ids plus dense
 *        iteration of the tagged entities.
 *
 * The set holds the ids of the entities carrying a "stunned" tag. Entity 3
 * recovering is an O(1) erase, and iteration walks the dense keys of the
 * entities still tagged.
 *
 * Expected output:
 *
 * \code
 * stunned: 4 entities, contains 42: true
 * still stunned: 10 7 42
 * \endcode
 */

#include <cstdint>
#include <print>

#include <nexenne/container/sparse_set.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::sparse_set_u32 stunned;
  for (std::uint32_t const id : {10u, 3u, 42u, 7u}) {
    stunned.insert(id);
  }
  std::println("stunned: {} entities, contains 42: {}", stunned.size(), stunned.contains(42u));

  stunned.erase(3u);

  std::print("still stunned:");
  for (auto const id : stunned.keys()) {
    std::print(" {}", id);
  }
  std::println("");
  return 0;
}
