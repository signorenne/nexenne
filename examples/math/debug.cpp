/**
 * @file
 * @brief Printing and hashing math types with the standard library.
 *
 *   1. std::format and std::println work directly on the math types.
 *   2. The strong scalar types print through the same layer, and the format
 *      spec applies to the value.
 *   3. to_string gives the same representation wherever a string is wanted.
 *   4. With <nexenne/math/hash.hpp> included the types double as hash-map keys.
 */

#include <print>
#include <unordered_map>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/fixed.hpp>
#include <nexenne/math/format.hpp>
#include <nexenne/math/hash.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  std::println("vector     = {}", nm::vector3_d{1.5, -2.0, 3.25});
  std::println("quaternion = {}", nm::quaternion_d::identity());
  std::println("matrix     = {}", nm::matrix3_d::identity());

  std::println("radians    = {:.4f}", nm::radians_d{1.5708});
  std::println("degrees    = {}", nm::degrees_d{90.0});
  std::println("fixed      = {}", nm::q16_16{1.5});

  std::println("to_string  = {}", nm::to_string(nm::vector2_i{7, 8}));

  std::unordered_map<nm::vector2_i, char const*> tiles;
  tiles[nm::vector2_i{0, 0}] = "spawn";
  tiles[nm::vector2_i{3, 4}] = "chest";
  std::println("tile (3,4) = {}", tiles.at(nm::vector2_i{3, 4}));
  std::println("distinct tiles = {}", tiles.size());
  return 0;
}
