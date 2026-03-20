/**
 * @file
 * @brief Printing and hashing math types with the standard library.
 */

#include <print>
#include <unordered_map>

#include <nexenne/math/format.hpp>
#include <nexenne/math/hash.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  // std::format / std::println work directly on the math types.
  std::println("vector     = {}", nm::vector3_d{1.5, -2.0, 3.25});
  std::println("quaternion = {}", nm::quaternion_d::identity());
  std::println("matrix     = {}", nm::matrix3_d::identity());

  // to_string is the same representation, usable anywhere a string is wanted.
  std::println("to_string  = {}", nm::to_string(nm::vector2_i{7, 8}));

  // The types double as hash-map keys once <nexenne/math/hash.hpp> is included.
  std::unordered_map<nm::vector2_i, char const*> tiles;
  tiles[nm::vector2_i{0, 0}] = "spawn";
  tiles[nm::vector2_i{3, 4}] = "chest";
  std::println("tile (3,4) = {}", tiles.at(nm::vector2_i{3, 4}));
  std::println("distinct tiles = {}", tiles.size());
  return 0;
}
