/**
 * @file
 * @brief Uniform draws, weighted choice, and shuffling with a fixed seed.
 *
 * Integer ranges, a fair coin, a weighted loot pick, and a deck shuffle, all
 * reproducible because the engine is seeded deterministically.
 */

#include <array>
#include <print>
#include <span>
#include <vector>

#include <nexenne/random/pcg.hpp>
#include <nexenne/random/sample.hpp>
#include <nexenne/random/uniform.hpp>

namespace {

namespace rnd = nexenne::random;

}  // namespace

auto main() -> int {
  rnd::pcg32 g{2024, 1};

  std::print("five d6 rolls:");
  for (int i{0}; i < 5; ++i) {
    std::print(" {}", rnd::uniform_int(g, 1, 6));
  }
  std::println("");

  // Weighted loot: common 70%, rare 25%, legendary 5%.
  std::array<double, 3> const loot_weights{70.0, 25.0, 5.0};
  std::array<char const*, 3> const loot{"common", "rare", "legendary"};
  std::array<int, 3> drops{};
  for (int i{0}; i < 1000; ++i) {
    ++drops[rnd::weighted_choice(loot_weights, g)];
  }
  std::println("1000 loot drops: {} common, {} rare, {} legendary", drops[0], drops[1], drops[2]);
  static_cast<void>(loot);

  std::vector<int> deck{1, 2, 3, 4, 5, 6, 7, 8};
  rnd::shuffle(deck, g);
  std::print("shuffled deck:");
  for (int const c : deck) {
    std::print(" {}", c);
  }
  std::println("");
  // five d6 rolls: 5 1 2 4 1
  // 1000 loot drops: 704 common, 249 rare, 47 legendary
  // shuffled deck: 7 5 8 6 1 4 2 3
  return 0;
}
