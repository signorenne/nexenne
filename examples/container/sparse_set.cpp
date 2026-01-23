/**
 * @file
 * @brief sparse_set as an ECS tag: O(1) membership over entity ids plus dense
 *        iteration of the tagged entities.
 */

#include <cstdint>
#include <print>

#include <nexenne/container/sparse_set.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::sparse_set_u32 stunned;  // entities currently carrying the "stunned" tag
  for (std::uint32_t const id : {10u, 3u, 42u, 7u}) {
    stunned.insert(id);
  }
  std::println("stunned: {} entities, contains 42: {}", stunned.size(), stunned.contains(42u));

  stunned.erase(3u);  // entity 3 recovered: O(1)

  std::print("still stunned:");
  for (auto const id : stunned.keys()) {
    std::print(" {}", id);
  }
  std::println("");
  // stunned: 4 entities, contains 42: true
  // still stunned: 10 7 42
  return 0;
}
