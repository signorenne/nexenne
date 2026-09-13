/**
 * @file
 * @brief Uniform draws, coins, weighted choice, shuffling, and reservoir picks.
 *
 * The grab-bag of "pick something" primitives, all reproducible because the
 * engine is seeded deterministically:
 *
 *   1. Bounded integers: uniform_int draws a closed range [lo, hi] with zero
 *      bias (Lemire's method), here five d6 rolls.
 *   2. Coins: bernoulli(g) is a fair coin from a single low bit (no
 *      rejection); bernoulli(g, p) is true with probability p, here a 5% crit.
 *   3. Weighted choice by linear scan: weighted_choice sums the weights and
 *      scans once per draw, O(n) each call but nothing to precompute; fine for
 *      a one-off pick over a few outcomes.
 *   4. Weighted choice by cumulative table: discrete_distribution builds the
 *      table once, then samples in O(log n) by binary search, the right pick
 *      when drawing from the same weights many times. probability(i) recovers
 *      the design intent, and an all-zero or empty table returns size() as a
 *      "no outcome" sentinel.
 *   5. Shuffle: an in-place Fisher-Yates.
 *   6. Reservoir sample: pick k items uniformly from a stream you can only
 *      traverse once and whose length you do not know up front (a log, a
 *      network feed). Algorithm R keeps each seen item with the right
 *      probability; here 3 of 100 in one pass.
 */

#include <array>
#include <cstddef>
#include <print>
#include <vector>

#include <nexenne/random/discrete.hpp>
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

  int heads{0};
  int crits{0};
  for (int i{0}; i < 1000; ++i) {
    heads += rnd::bernoulli(g) ? 1 : 0;
    crits += rnd::bernoulli(g, 0.05) ? 1 : 0;
  }
  std::println("1000 fair flips: {} heads (~500); 1000 5%-crits: {} (~50)", heads, crits);

  std::array<double, 3> const loot_weights{70.0, 25.0, 5.0};
  std::array<int, 3> drops{};
  for (int i{0}; i < 1000; ++i) {
    ++drops[rnd::weighted_choice(loot_weights, g)];
  }
  std::println("1000 loot drops: {} common, {} rare, {} legendary", drops[0], drops[1], drops[2]);

  rnd::discrete_distribution<double> table{{70.0, 25.0, 5.0}};
  std::array<int, 3> table_drops{};
  for (int i{0}; i < 1000; ++i) {
    ++table_drops[table.sample(g)];
  }
  std::println(
    "discrete_distribution 1000 drops: {} / {} / {}  (targets {:.0f}% / {:.0f}% / {:.0f}%)",
    table_drops[0],
    table_drops[1],
    table_drops[2],
    table.probability(0) * 100.0,
    table.probability(1) * 100.0,
    table.probability(2) * 100.0
  );
  rnd::discrete_distribution<double> empty_table{{0.0, 0.0}};
  std::println("all-zero table sample == size() sentinel: {}", empty_table.sample(g) == 2);

  std::vector<int> deck{1, 2, 3, 4, 5, 6, 7, 8};
  rnd::shuffle(deck, g);
  std::print("shuffled deck:");
  for (int const c : deck) {
    std::print(" {}", c);
  }
  std::println("");

  std::vector<int> stream(100);
  for (std::size_t i{0}; i < stream.size(); ++i) {
    stream[i] = static_cast<int>(i);
  }
  auto const picks{rnd::reservoir_sample(stream, 3, g)};
  std::print("reservoir sample (3 of 100):");
  for (int const p : picks) {
    std::print(" {}", p);
  }
  std::println("");
  return 0;
}
