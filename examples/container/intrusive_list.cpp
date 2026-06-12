/**
 * @file
 * @brief intrusive_list as an active-object set: O(1) link and unlink, no
 *        allocation.
 *
 * The enemies own their own storage; the list only threads them together through
 * the hook embedded in each, so linking and unlinking allocate nothing.
 */

#include <print>

#include <nexenne/container/intrusive_list.hpp>

namespace {

namespace cn = nexenne::container;

struct enemy : cn::intrusive_list_hook<enemy> {
  int id;
  int hp;

  enemy(int id_init, int hp_init) noexcept : id{id_init}, hp{hp_init} {}
};

}  // namespace

auto main() -> int {
  enemy a{1, 100};
  enemy b{2, 0};  // already dead
  enemy c{3, 50};

  cn::intrusive_list<enemy> active;
  active.push_back(a);
  active.push_back(b);
  active.push_back(c);
  std::println("active: {}", active.size());

  active.erase(b);  // b died: O(1) unlink, no search, no free

  std::print("remaining:");
  for (auto const& e : active) {
    std::print(" {}(hp {})", e.id, e.hp);
  }
  std::println("");
  // active: 3
  // remaining: 1(hp 100) 3(hp 50)
  return 0;
}
