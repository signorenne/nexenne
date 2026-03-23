/**
 * @file
 * @brief slot_map as a resource registry: stable handles that survive recycling.
 *
 * Each insert hands back an opaque key (slot index plus generation). The key
 * stays valid across other inserts and erases, and a key to an erased-then-
 * recycled slot reads as absent rather than silently aliasing the new occupant.
 * This tour covers the full handle lifecycle: insert, find-and-mutate through the
 * returned pointer, erase, the generation guard that makes recycling safe, live-
 * only iteration, and clear's wholesale invalidation.
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

  // find returns a pointer to the live element (or nullptr): a checked,
  // never-undefined lookup that doubles as a mutation handle.
  if (auto* const t{textures.find(stone)}) {
    *t = "stone_hires.png";  // rename in place through the handle
  }
  std::println("stone now: {}", *textures.find(stone));

  textures.erase(grass);  // free the slot; grass is now a stale handle

  // A new insert recycles grass's slot, but the old handle does not see it: the
  // slot's generation was bumped on erase, so grass's generation no longer
  // matches. This is the ABA guard that makes handle recycling safe.
  auto const water{textures.insert("water.png")};

  std::println("live textures: {}", textures.size());
  std::println("stone still valid: {}", textures.contains(stone));
  std::println("grass handle after erase: {}", textures.contains(grass));
  std::println("recycled slot reused grass index: {}", water.index() == grass.index());
  std::println(
    "grass and water share an index but not a generation: {}",
    grass.generation() != water.generation()
  );
  std::println("find on the stale grass handle: {}", textures.find(grass) == nullptr);

  // erase reports whether it removed anything; a second erase of the same key is
  // a harmless no-op, so double-free of a handle is safe, not undefined.
  std::println("re-erasing grass removed something: {}", textures.erase(grass));

  // Iteration walks only the live elements, in slot order, skipping the vacancy
  // grass left behind. The order is the storage order, not insertion order.
  std::print("all live textures:");
  for (auto const& name : textures) {
    std::print(" {}", name);
  }
  std::println("");

  // clear empties the map and bumps every generation, so every outstanding key -
  // including stone and water - is now stale. Capacity is retained for reuse.
  textures.clear();
  std::println(
    "after clear: size {}, stone valid {}, water valid {}",
    textures.size(),
    textures.contains(stone),
    textures.contains(water)
  );
  // stone now: stone_hires.png
  // live textures: 2
  // stone still valid: true
  // grass handle after erase: false
  // recycled slot reused grass index: true
  // grass and water share an index but not a generation: true
  // find on the stale grass handle: true
  // re-erasing grass removed something: false
  // all live textures: water.png stone_hires.png
  // after clear: size 0, stone valid false, water valid false
  return 0;
}
