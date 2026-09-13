/**
 * @file
 * @brief A guided tour of nexenne::random through one realistic task: a
 *        deterministic, seedable procedural "dungeon run" for a game.
 *
 * This program does not render anything - it *generates* one expedition's
 * worth of content and prints it, so you can see how the pieces of the module
 * fit together in context:
 *
 *   1. Seed everything. seed_from_string hashes a designer-friendly phrase
 *      into a seed with the same mixing on every toolchain (std::seed_seq is
 *      implementation-defined). seed_sequence then derives one independent
 *      sub-seed per subsystem via SplitMix64, so one extra loot draw never
 *      shifts the monster or party streams. Each subsystem gets its own
 *      xoshiro256ss, the 64-bit-native engine; its constructor substitutes
 *      the one forbidden (zero) seed, so any sub-seed is safe.
 *   2. Lay out the rooms. uniform_int is the bias-free, portable replacement
 *      for std::uniform_int_distribution (Lemire's nearly-divisionless
 *      sampler over a closed range). Room kinds are not equally likely, so a
 *      discrete_distribution builds a cumulative table once, samples in
 *      O(log N), and probability(i) prints the design intent next to the rolls.
 *   3. Roll the loot. Each chest rolls a rarity off a weighted loot table, then
 *      a gamma-distributed gold payout: strictly positive, right-skewed, with
 *      mean shape * scale (2 * 100 = 200 gold), which is how a designer reasons
 *      about a payout curve. A normal would allow negative gold and an
 *      exponential has no typical-value hump. Rare and legendary chests pay a
 *      multiplier on the same curve.
 *   4. Schedule monsters. Spawns are a Poisson process: exponential_distribution
 *      gives the gap between spawns (rate 1.5 per minute, so a mean gap of
 *      1 / 1.5 minutes) and poisson_distribution the count per wave (mean 4).
 *   5. Roll the party. Ability scores cluster around a mean (12, stddev 3), the
 *      textbook normal; normal_distribution caches the Box-Muller pair's second
 *      variate, half the cost of the free normal() over many draws. Scores are
 *      clamped to the 3 to 18 die range. shuffle is an in-place, unbiased
 *      Fisher-Yates for the initiative order, and reservoir_sample with k = 1
 *      picks the MVP: the tool when a stream's length is unknown.
 *   6. Estimate the odds. Each hero survives with probability p and the party
 *      wins if any does, 1 - (1 - p)^k in closed form. bernoulli trials
 *      estimate it by simulation, and the estimate is checked against the
 *      exact value.
 *   7. Prove determinism. Re-running the same phrase must reproduce the digest
 *      bit for bit, and a different phrase must differ. format.hpp then prints
 *      an engine's exact state and a distribution's parameters for a
 *      reproducibility report (opt-in, because std::format is heavy).
 *
 * Read it top to bottom.
 *
 * Reproducibility is the throughline. Every stochastic choice flows from one
 * seed; nothing reads the clock or the OS RNG. The same seed therefore replays
 * the same expedition on every run and (for the integer/engine paths) on every
 * platform - exactly what you want for replay logs, networked lockstep, and
 * deterministic tests. The std distributions deliberately do NOT guarantee
 * this, which is why this module reimplements them.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <print>
#include <string_view>
#include <vector>

#include <nexenne/random/discrete.hpp>
#include <nexenne/random/exponential.hpp>
#include <nexenne/random/format.hpp>
#include <nexenne/random/gamma.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/poisson.hpp>
#include <nexenne/random/sample.hpp>
#include <nexenne/random/seed_seq.hpp>
#include <nexenne/random/uniform.hpp>
#include <nexenne/random/xoshiro.hpp>

namespace rng = nexenne::random;

namespace {

/**
 * @brief Generates one reproducible expedition and digests every roll.
 *
 * @param seed_phrase Human-readable phrase the whole run is seeded from.
 * @param verbose Whether to print each step of the run.
 *
 * @return A 64-bit digest folded from every roll, so two runs with the same
 *         seed can be compared bit for bit.
 *
 * @pre None.
 * @post Equal \p seed_phrase values give equal digests.
 */
auto run_expedition(std::string_view const seed_phrase, bool const verbose) -> std::uint64_t {
  // FNV-1a fold: XOR then multiply by the FNV prime, so the digest is order-sensitive.
  std::uint64_t digest{0xCBF29CE484222325ULL};
  auto const fold{[&digest](std::uint64_t const x) {
    digest ^= x;
    digest *= 0x100000001B3ULL;
  }};

  auto const master{rng::seed_from_string(seed_phrase)};
  constexpr std::size_t subsystems{4};
  auto const seeds{rng::seed_sequence<subsystems>(master)};

  rng::xoshiro256ss layout_rng{seeds[0]};
  rng::xoshiro256ss loot_rng{seeds[1]};
  rng::xoshiro256ss monster_rng{seeds[2]};
  rng::xoshiro256ss party_rng{seeds[3]};

  fold(master);

  if (verbose) {
    std::println("== seed ==");
    std::println("  phrase            \"{}\"", seed_phrase);
    std::println("  master seed       {:#018x}", master);
    std::println("  subsystem seeds   {} independent streams", subsystems);
  }

  constexpr std::array<std::string_view, 4> kind_names{"corridor", "chamber", "shrine", "vault"};
  rng::discrete_distribution<double> room_kinds{{50.0, 30.0, 15.0, 5.0}};

  auto const room_count{rng::uniform_int(layout_rng, 6, 12)};
  std::array<std::size_t, 4> kind_tally{};
  for (int r{0}; r < room_count; ++r) {
    auto const kind{room_kinds.sample(layout_rng)};
    ++kind_tally[kind];
    fold(static_cast<std::uint64_t>(kind));
  }
  fold(static_cast<std::uint64_t>(room_count));

  if (verbose) {
    std::println("== layout ==");
    std::println("  rooms             {}", room_count);
    for (std::size_t k{0}; k < kind_names.size(); ++k) {
      std::println(
        "  {:<10} x{:<3}   (target {:.0f}%)",
        kind_names[k],
        kind_tally[k],
        room_kinds.probability(k) * 100.0
      );
    }
  }

  [[maybe_unused]] constexpr std::array<std::string_view, 3> rarity_names{
    "common", "rare", "legendary"
  };
  rng::discrete_distribution<double> rarity{{70.0, 25.0, 5.0}};
  rng::gamma_distribution<double> gold{2.0, 100.0};

  auto const chests{rng::uniform_int(loot_rng, 2, 5)};
  std::array<std::size_t, 3> rarity_tally{};
  double total_gold{0.0};
  for (int c{0}; c < chests; ++c) {
    auto const tier{rarity.sample(loot_rng)};
    ++rarity_tally[tier];
    auto const mult{tier == 2 ? 5.0 : tier == 1 ? 2.0 : 1.0};
    auto const payout{gold.sample(loot_rng) * mult};
    total_gold += payout;
    fold(static_cast<std::uint64_t>(payout));
  }

  if (verbose) {
    std::println("== loot ==");
    std::println("  chests            {}", chests);
    std::println(
      "  rarities          {} common, {} rare, {} legendary",
      rarity_tally[0],
      rarity_tally[1],
      rarity_tally[2]
    );
    std::println("  total gold        {:.0f}", total_gold);
  }

  rng::exponential_distribution<double> spawn_gap{1.5};
  rng::poisson_distribution<int> wave_size{4.0};

  double clock_minutes{0.0};
  int total_monsters{0};
  constexpr std::size_t waves{4};
  std::array<double, waves> wave_times{};
  std::array<int, waves> wave_counts{};
  for (std::size_t w{0}; w < waves; ++w) {
    clock_minutes += spawn_gap.sample(monster_rng);
    auto const n{wave_size.sample(monster_rng)};
    wave_times[w] = clock_minutes;
    wave_counts[w] = n;
    total_monsters += n;
    fold(static_cast<std::uint64_t>(clock_minutes * 1000.0));
    fold(static_cast<std::uint64_t>(n));
  }

  if (verbose) {
    std::println("== monsters ==");
    for (std::size_t w{0}; w < waves; ++w) {
      std::println("  wave {} @ {:5.2f} min   x{} spawns", w + 1, wave_times[w], wave_counts[w]);
    }
    std::println("  total monsters    {}", total_monsters);
  }

  constexpr std::array<std::string_view, 4> heroes{"Aria", "Borin", "Cael", "Dusk"};
  rng::normal_distribution<double> ability{12.0, 3.0};

  std::array<int, heroes.size()> scores{};
  for (std::size_t h{0}; h < heroes.size(); ++h) {
    auto const raw{ability.sample(party_rng)};
    auto const clamped{raw < 3.0 ? 3.0 : raw > 18.0 ? 18.0 : raw};
    scores[h] = static_cast<int>(clamped + 0.5);
    fold(static_cast<std::uint64_t>(scores[h]));
  }

  std::array<std::size_t, heroes.size()> initiative{0, 1, 2, 3};
  rng::shuffle(initiative, party_rng);
  for (auto const i : initiative) {
    fold(static_cast<std::uint64_t>(i));
  }

  auto const mvp{rng::reservoir_sample(std::array<std::size_t, 4>{0, 1, 2, 3}, 1, party_rng)};
  fold(static_cast<std::uint64_t>(mvp.front()));

  if (verbose) {
    std::println("== party ==");
    for (std::size_t h{0}; h < heroes.size(); ++h) {
      std::println("  {:<6} ability {}", heroes[h], scores[h]);
    }
    std::print("  initiative        ");
    for (auto const i : initiative) {
      std::print("{} ", heroes[i]);
    }
    std::println("");
    std::println("  surprise MVP      {}", heroes[mvp.front()]);
  }

  constexpr double p_survive{0.45};
  constexpr int k_heroes{4};
  constexpr int trials{200000};
  int party_wins{0};
  for (int t{0}; t < trials; ++t) {
    bool any{false};
    for (int h{0}; h < k_heroes; ++h) {
      any = rng::bernoulli(party_rng, p_survive) || any;
    }
    party_wins += any ? 1 : 0;
  }
  auto const estimate{static_cast<double>(party_wins) / trials};
  auto const exact{1.0 - std::pow(1.0 - p_survive, k_heroes)};
  auto const err{estimate - exact > 0.0 ? estimate - exact : exact - estimate};
  fold(static_cast<std::uint64_t>(party_wins));

  if (verbose) {
    std::println("== win odds ==");
    std::println("  Monte-Carlo       {:.4f}  ({} trials)", estimate, trials);
    std::println("  closed form       {:.4f}  = 1 - (1-p)^k", exact);
    std::println("  abs error         {:.4f}  ({})", err, err < 0.01 ? "within tolerance" : "HIGH");
  }

  return digest;
}

}  // namespace

auto main() -> int {
  auto const digest_a{run_expedition("crypt-of-echoes", true)};

  auto const digest_b{run_expedition("crypt-of-echoes", false)};
  auto const digest_c{run_expedition("hall-of-whispers", false)};

  std::println("== determinism ==");
  std::println("  digest (run 1)    {:#018x}", digest_a);
  std::println("  digest (run 2)    {:#018x}", digest_b);
  std::println("  same seed equal   {}", digest_a == digest_b);
  std::println("  other seed differs {}", digest_a != digest_c);

  if (digest_a != digest_b) {
    std::println("DETERMINISM BROKEN");
    return 1;
  }

  rng::xoshiro256ss const reporter{0x00C0'FFEEu};
  rng::normal_distribution<double> const scores{10.0, 3.0};
  std::println("== formatting ==");
  std::println("  engine   {}", reporter);
  std::println("  scores   {}", scores);

  std::println("\nThat is the whole module in one expedition: seeding, uniform and");
  std::println("weighted draws, gamma/normal/exponential/poisson distributions,");
  std::println("shuffling, reservoir sampling, Monte-Carlo, and reproducibility.");
  return 0;
}
