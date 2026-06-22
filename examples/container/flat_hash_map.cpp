/**
 * @file
 * @brief flat_hash_map as an asset registry: O(1) average name to id lookup.
 *
 * A general hashable-key map over one contiguous, linear-probed slot array:
 * roughly one allocation and one cache miss per lookup, several times faster than
 * std::unordered_map's node-per-entry layout. This tour covers the insertion
 * trio (insert keeps an existing value, insert_or_assign overwrites, operator[]
 * default-inserts), checked lookup, erase with its tombstone, capacity growth and
 * load factor across a rehash, and unordered iteration.
 */

#include <print>
#include <string>

#include <nexenne/container/flat_hash_map.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::flat_hash_map<std::string, int> assets;
  std::println("insert player.png is fresh: {}", assets.insert("player.png", 1));
  assets.insert("enemy.png", 2);
  assets.insert("music.ogg", 3);

  // The three ways to write differ on a key collision:
  //  - insert leaves an existing value untouched (returns false),
  //  - insert_or_assign overwrites it,
  //  - operator[] default-inserts then hands back a mutable reference.
  std::println("insert player.png again is fresh: {}", assets.insert("player.png", 99));
  assets.insert_or_assign("player.png", 10);  // re-register: overwrites
  assets["sfx.wav"] += 5;                     // default-insert 0, then += 5

  std::println("{} assets registered", assets.size());
  if (auto const* const id{assets.find("enemy.png")}) {
    std::println("enemy.png -> {}", *id);
  }
  std::println("player.png -> {}", *assets.find("player.png"));
  std::println("sfx.wav -> {}", *assets.find("sfx.wav"));
  std::println(
    "has 'missing.png': {} (count {})", assets.contains("missing.png"), assets.count("missing.png")
  );

  // erase removes the entry and leaves a tombstone a later probe skips; the slot
  // is reclaimed on the next rehash. find on the erased key now misses.
  std::println("erase music.ogg: {}", assets.erase("music.ogg"));
  std::println("erase music.ogg again: {}", assets.erase("music.ogg"));  // already gone
  std::println("music.ogg findable: {}", assets.find("music.ogg") != nullptr);

  // Capacity is a power of two and the table rehashes at 7/8 load. Insert past
  // the threshold and watch the slot count double while the load factor stays
  // bounded. A rehash invalidates pointers from earlier find() calls.
  std::println(
    "before fill: size {}, capacity {}, load {:.2f}",
    assets.size(),
    assets.capacity(),
    assets.load_factor()
  );
  for (int i{0}; i < 40; ++i) {
    assets.insert("tex_" + std::to_string(i), 1000 + i);
  }
  std::println(
    "after fill: size {}, capacity {}, load {:.2f}",
    assets.size(),
    assets.capacity(),
    assets.load_factor()
  );

  // Iteration visits every live entry once, in an unspecified (slot) order; we
  // just total the ids to prove the count without depending on order.
  long sum{0};
  for (auto const& [name, id] : assets) {
    sum += id;
  }
  std::println("iterated {} entries, id sum {}", assets.size(), sum);
  // insert player.png is fresh: true
  // insert player.png again is fresh: false
  // 4 assets registered
  // enemy.png -> 2
  // player.png -> 10
  // sfx.wav -> 5
  // has 'missing.png': false (count 0)
  // erase music.ogg: true
  // erase music.ogg again: false
  // music.ogg findable: false
  // before fill: size 3, capacity 16, load 0.19
  // after fill: size 43, capacity 64, load 0.67
  // iterated 43 entries, id sum 40797
  return 0;
}
