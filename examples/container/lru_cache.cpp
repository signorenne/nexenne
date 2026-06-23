/**
 * @file
 * @brief lru_cache as a bounded asset cache: keep the hottest N resident.
 *
 * Each get and put marks the entry most-recently-used; once the cache is full a
 * new key evicts the least-recently-used one. The node pool is fixed, so the
 * steady state allocates nothing.
 */

#include <print>
#include <string>

#include <nexenne/container/lru_cache.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::lru_cache<std::string, int, 2> textures{};  // room for two resident textures
  textures.put("grass.png", 1);
  textures.put("stone.png", 2);

  nexenne::utility::discard(textures.get("grass.png"));  // touch grass: it becomes MRU
  textures.put("water.png", 3);                          // full: evicts LRU (stone)

  std::println("resident: {}", textures.size());
  std::println("grass cached: {}", textures.contains("grass.png"));
  std::println("stone cached: {}", textures.contains("stone.png"));
  std::println("least recently used now: {}", *textures.lru_key());
  // resident: 2
  // grass cached: true
  // stone cached: false
  // least recently used now: grass.png
  return 0;
}
