/**
 * @file
 * @brief slot_map as a resource registry: stable handles that survive recycling.
 *
 * Each insert hands back an opaque key (slot index plus generation). The key
 * stays valid across other inserts and erases, and a key to an erased-then-
 * recycled slot reads as absent rather than silently aliasing the new occupant.
 */

#include <print>
#include <string>

#include <nexenne/container/slot_map.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::slot_map<std::string> textures;
  auto const grass{textures.insert("grass.png")};
  auto const stone{textures.insert("stone.png")};

  textures.erase(grass);  // free the slot; grass is now a stale handle

  // A new insert recycles grass's slot, but the old handle does not see it.
  auto const water{textures.insert("water.png")};

  std::println("live textures: {}", textures.size());
  std::println("stone still valid: {}", textures.contains(stone));
  std::println("grass handle after erase: {}", textures.contains(grass));
  std::println("recycled slot reused grass index: {}", water.index() == grass.index());
  // live textures: 2
  // stone still valid: true
  // grass handle after erase: false
  // recycled slot reused grass index: true
  return 0;
}
