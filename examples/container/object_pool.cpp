/**
 * @file
 * @brief object_pool as a particle pool: spawn and recycle from a fixed roster.
 *
 * N particles are reserved up front; spawning is an O(1) acquire-and-construct
 * and a death is an O(1) destroy-and-recycle, with no per-particle allocation.
 */

#include <print>

#include <nexenne/container/object_pool.hpp>

namespace {

namespace cn = nexenne::container;

struct particle {
  int x;
  int life;

  particle(int x_init, int life_init) noexcept : x{x_init}, life{life_init} {}
};

}  // namespace

auto main() -> int {
  cn::object_pool<particle, 3> pool;

  auto const a{pool.emplace(0, 10)};
  auto const b{pool.emplace(5, 20)};
  auto const c{pool.emplace(9, 30)};
  std::println("spawned: {} live, full: {}", pool.size(), pool.full());

  if (b.has_value()) {
    static_cast<void>(pool.destroy(*b));  // one dies, its slot recycles
  }
  std::println("one died: {} live", pool.size());

  auto const d{pool.emplace(2, 40)};  // reuses the recycled slot
  std::println("respawned: {} live, peak: {}", pool.size(), pool.high_water_mark());

  if (a.has_value()) {
    static_cast<void>(pool.destroy(*a));
  }
  if (c.has_value()) {
    static_cast<void>(pool.destroy(*c));
  }
  if (d.has_value()) {
    static_cast<void>(pool.destroy(*d));
  }
  // spawned: 3 live, full: true
  // one died: 2 live
  // respawned: 3 live, peak: 3
  return 0;
}
