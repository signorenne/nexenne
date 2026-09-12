/**
 * @file
 * @brief flat_hash_map as an asset registry: O(1) average name to id lookup.
 *
 * A general hashable-key map over one contiguous, linear-probed slot array:
 * roughly one allocation and one cache miss per lookup, several times faster than
 * std::unordered_map's node-per-entry layout. The tour walks five steps:
 *
 * 1. The insertion trio differs on a key collision: insert leaves an existing
 *    value untouched and returns false, insert_or_assign overwrites it, and
 *    operator[] default-inserts, then hands back a mutable reference.
 * 2. find and contains are checked lookups: a miss is a null pointer or false.
 * 3. erase removes the entry and shifts later entries of its probe run back, so
 *    no tombstone is left. An erase can move other entries: never erase while
 *    iterating.
 * 4. Capacity is a power of two and the table rehashes at 7/8 load: filling
 *    past the threshold doubles the slot count while the load factor stays
 *    bounded. A rehash invalidates pointers from earlier find() calls.
 * 5. Iteration visits every live entry once, in an unspecified slot order, so
 *    the program totals the ids instead of depending on order.
 *
 * Expected output:
 *
 * \code
 * insert player.png is fresh: true
 * insert player.png again is fresh: false
 * 4 assets registered
 * enemy.png -> 2
 * player.png -> 10
 * sfx.wav -> 5
 * has 'missing.png': false (count 0)
 * erase music.ogg: true
 * erase music.ogg again: false
 * music.ogg findable: false
 * before fill: size 3, capacity 16, load 0.19
 * after fill: size 43, capacity 64, load 0.67
 * iterated 43 entries, id sum 40797
 * \endcode
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

  std::println("insert player.png again is fresh: {}", assets.insert("player.png", 99));
  assets.insert_or_assign("player.png", 10);
  assets["sfx.wav"] += 5;

  std::println("{} assets registered", assets.size());
  if (auto const* const id{assets.find("enemy.png")}) {
    std::println("enemy.png -> {}", *id);
  }
  std::println("player.png -> {}", *assets.find("player.png"));
  std::println("sfx.wav -> {}", *assets.find("sfx.wav"));
  std::println(
    "has 'missing.png': {} (count {})", assets.contains("missing.png"), assets.count("missing.png")
  );

  std::println("erase music.ogg: {}", assets.erase("music.ogg"));
  std::println("erase music.ogg again: {}", assets.erase("music.ogg"));
  std::println("music.ogg findable: {}", assets.find("music.ogg") != nullptr);

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

  long sum{0};
  for (auto const& [name, id] : assets) {
    sum += id;
  }
  std::println("iterated {} entries, id sum {}", assets.size(), sum);
  return 0;
}
