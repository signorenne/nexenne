/**
 * @file
 * @brief dense_map as an ECS component store: entity id to component, dense walk.
 *
 * Integer entity ids key a sparse_set, while the components live packed in a
 * parallel vector, so a system iterates them as one contiguous, cache-friendly
 * sweep regardless of how sparse the ids are.
 */

#include <cstdint>
#include <print>

#include <nexenne/container/dense_map.hpp>

namespace {

namespace cn = nexenne::container;

struct velocity {
  float dx;
  float dy;
};

}  // namespace

auto main() -> int {
  cn::dense_map<std::uint32_t, velocity> velocities;
  velocities.insert(10, {1.0F, 0.0F});
  velocities.emplace(2, 0.0F, -2.0F);  // construct in place
  velocities.insert(7, {3.0F, 3.0F});

  velocities.erase(2);  // swap-pop: entity 7 moves into the freed slot

  // A "system": one dense pass over the live components.
  float total_speed{0.0F};
  for (auto const [id, v] : velocities) {
    total_speed += v.dx + v.dy;
  }

  std::println("{} entities have a velocity", velocities.size());
  std::println("has entity 2: {}", velocities.contains(2));
  std::println("total speed component sum: {}", total_speed);
  // 2 entities have a velocity
  // has entity 2: false
  // total speed component sum: 7
  return 0;
}
