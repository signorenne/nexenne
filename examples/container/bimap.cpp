/**
 * @file
 * @brief bimap as a two-way registry: id to name and name to id, both O(1).
 *
 * Each side stays unique, so a lookup works in either direction in amortised
 * constant time without scanning, and replace rebinds while evicting any
 * conflicting entry.
 */

#include <print>
#include <string>

#include <nexenne/container/bimap.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::bimap<int, std::string> names;
  names.insert(1, "player");
  names.insert(2, "enemy");
  names.insert(3, "npc");

  // Rebind id 2 to a new name, evicting its old "enemy" binding.
  auto const displaced{names.replace(2, "boss")};

  std::println("{} ids registered", names.size());
  std::println("id 1 -> {}", *names.find_by_left(1));
  std::println("name 'boss' -> {}", *names.find_by_right("boss"));
  std::println("old name 'enemy' still bound: {}", names.contains_right("enemy"));
  std::println("replace displaced {} entries", displaced);
  // 3 ids registered
  // id 1 -> player
  // name 'boss' -> 2
  // old name 'enemy' still bound: false
  // replace displaced 1 entries
  return 0;
}
