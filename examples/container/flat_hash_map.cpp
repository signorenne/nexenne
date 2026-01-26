/**
 * @file
 * @brief flat_hash_map as an asset registry: O(1) average name to id lookup.
 *
 * A general hashable-key map: register assets by name, overwrite with
 * insert_or_assign, and look them up in amortised constant time over contiguous
 * storage.
 */

#include <print>
#include <string>

#include <nexenne/container/flat_hash_map.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::flat_hash_map<std::string, int> assets;
  assets.insert("player.png", 1);
  assets.insert("enemy.png", 2);
  assets.insert("music.ogg", 3);
  assets.insert_or_assign("player.png", 10);  // re-register: overwrites

  std::println("{} assets registered", assets.size());
  if (auto const* const id{assets.find("enemy.png")}) {
    std::println("enemy.png -> {}", *id);
  }
  std::println("player.png -> {}", *assets.find("player.png"));
  std::println("has 'missing.png': {}", assets.contains("missing.png"));
  // 3 assets registered
  // enemy.png -> 2
  // player.png -> 10
  // has 'missing.png': false
  return 0;
}
