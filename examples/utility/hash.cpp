/**
 * @file
 * @brief Build a composite std::hash for an aggregate, plus range hashing.
 *
 * A 3D grid cell needs a hash so it can live in an unordered_map: we fold its
 * fields with hash_args, and show a sequence hashes order-sensitively via
 * hash_range.
 */

#include <array>
#include <cstdint>
#include <print>
#include <unordered_map>

#include <nexenne/utility/hash.hpp>

namespace util = nexenne::utility;

struct cell {
  std::int32_t x{};
  std::int32_t y{};
  std::int32_t z{};

  auto operator==(cell const&) const -> bool = default;
};

template <>
struct std::hash<cell> {
  auto operator()(cell const& c) const noexcept -> std::size_t {
    return util::hash_args(c.x, c.y, c.z);
  }
};

auto main() -> int {
  auto grid{std::unordered_map<cell, char const*>{}};
  grid[cell{1, 2, 3}] = "spawn";
  grid[cell{0, 0, 0}] = "origin";

  std::println("cells stored: {}", grid.size());
  std::println("cell(1,2,3) -> {}", grid.at(cell{1, 2, 3}));

  auto const forward{std::array<int, 3>{1, 2, 3}};
  auto const reversed{std::array<int, 3>{3, 2, 1}};
  std::println(
    "hash_range is order-sensitive: {}", util::hash_range(forward) != util::hash_range(reversed)
  );
  return 0;
}
