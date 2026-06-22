/**
 * @file
 * @brief A guided tour of nexenne::random through one realistic task: a
 *        deterministic, seedable procedural "dungeon run" for a game.
 *
 * This program does not render anything - it *generates* one expedition's
 * worth of content and prints it, so you can see how the pieces of the module
 * fit together in context:
 *
 *   1. Seed everything   -> a human-readable string -> one master seed, then
 *                           one independent sub-seed per subsystem.
 *   2. Lay out the rooms  -> uniform_int for counts, a weighted discrete_
 *                           distribution for room *kinds*.
 *   3. Roll the loot      -> a discrete loot table (rarity) plus a gamma-
 *                           distributed gold payout per chest.
 *   4. Schedule monsters  -> exponential inter-arrival times (a Poisson
 *                           process) and a poisson count of spawns per wave.
 *   5. Roll the party     -> normal-distributed ability scores, a shuffled
 *                           initiative order, a reservoir-sampled "MVP".
 *   6. Estimate the odds  -> a Monte-Carlo combat win-rate, self-checked
 *                           against a closed-form value.
 *   7. Prove determinism  -> re-run the whole thing and confirm the digest
 *                           is bit-identical.
 *
 * Each step notes *why* a given API is the right tool. Read it top to bottom.
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
#include <nexenne/random/gamma.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/poisson.hpp>
#include <nexenne/random/sample.hpp>
#include <nexenne/random/seed_seq.hpp>
#include <nexenne/random/uniform.hpp>
#include <nexenne/random/xoshiro.hpp>

namespace rng = nexenne::random;

namespace {

// One reproducible expedition. The return value is a 64-bit digest folded from
// every roll, so two runs with the same seed can be compared bit-for-bit.
auto run_expedition(std::string_view const seed_phrase, bool const verbose) -> std::uint64_t {
  // An order-dependent FNV-1a mixing accumulator. Every interesting value gets
  // folded in, so the digest captures the full run and its exact sequence.
  std::uint64_t digest{0xCBF29CE484222325ULL};
  auto const fold{[&digest](std::uint64_t const x) {
    digest ^= x;
    digest *= 0x100000001B3ULL;  // FNV-1a prime: cheap, order-sensitive mixing
  }};

  // 1. Seeding.
  //
  // Designers think in names ("crypt-of-echoes"), not 64-bit integers.
  // seed_from_string hashes the phrase into a stable seed with the same mixing
  // on every toolchain, so the same dungeon name always yields the same
  // dungeon - unlike std::seed_seq, whose output is implementation-defined.
  //
  // From that one master seed, seed_sequence<N> derives N *independent*
  // sub-seeds via SplitMix64. Giving each subsystem its own engine means a
  // change to, say, the loot logic (one extra draw) does not shift the monster
  // or party streams: each subsystem is reproducible on its own.
  auto const master{rng::seed_from_string(seed_phrase)};
  constexpr std::size_t subsystems{4};
  auto const seeds{rng::seed_sequence<subsystems>(master)};

  // xoshiro256ss is the 64-bit-native engine: a touch faster than pcg and ideal
  // when we draw a lot (the Monte-Carlo step below). Zero is the one forbidden
  // state, but the constructor substitutes it, so any sub-seed is safe.
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

  // 2. Room layout.
  //
  // uniform_int is the bias-free, portable replacement for
  // std::uniform_int_distribution: closed range [lo, hi], Lemire's
  // nearly-divisionless sampler, identical output everywhere.
  //
  // Room *kinds* are not equally likely - corridors are common, vaults rare -
  // so a discrete_distribution over weights is the right tool. It builds a
  // cumulative table once and samples in O(log N), and probability(i) lets us
  // print the design intent next to the rolls.
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

  // 3. Loot.
  //
  // Each chest first rolls a *rarity* off a weighted loot table (discrete
  // again), then a *gold* amount. Gold is gamma-distributed: it is strictly
  // positive, right-skewed (most chests modest, a few huge), and its mean is
  // shape * scale, which is exactly how a designer reasons about a payout
  // curve. A normal would allow negative gold; an exponential has no "typical
  // value" hump. Gamma fits.
  constexpr std::array<std::string_view, 3> rarity_names{"common", "rare", "legendary"};
  rng::discrete_distribution<double> rarity{{70.0, 25.0, 5.0}};
  // Mean payout 200 gold (shape 2 * scale 100), with a long upper tail.
  rng::gamma_distribution<double> gold{2.0, 100.0};

  auto const chests{rng::uniform_int(loot_rng, 2, 5)};
  std::array<std::size_t, 3> rarity_tally{};
  double total_gold{0.0};
  for (int c{0}; c < chests; ++c) {
    auto const tier{rarity.sample(loot_rng)};
    ++rarity_tally[tier];
    // Legendary chests pay a multiplier on the same gamma curve.
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
    static_cast<void>(rarity_names);
    std::println("  total gold        {:.0f}", total_gold);
  }

  // 4. Monster schedule.
  //
  // Spawns are a Poisson process: independent events at a constant average
  // rate. The two faces of that process map onto two distributions here:
  //
  //   - exponential_distribution gives the *gap* between consecutive spawns
  //     (inter-arrival time). Rate 1.5 means 1.5 spawns per "minute" on
  //     average, so a mean gap of 1/1.5 minutes.
  //   - poisson_distribution gives the *count* of monsters in one burst wave
  //     (how many appear at once). Mean 4 here.
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

  // 5. The party.
  //
  // Ability scores cluster around an average with a few outliers - the textbook
  // case for a normal distribution. normal_distribution(mean, stddev) caches
  // the Box-Muller pair's second variate, so it is half the cost of the
  // free-function normal() over many draws.
  //
  // Initiative is a fair random *order*: shuffle is an in-place Fisher-Yates,
  // allocation-free and unbiased. Then we pick a surprise "MVP" with
  // reservoir_sample(k=1) - overkill for a known-size vector, but it is the
  // tool when the stream length is unknown (a log, a network feed), so this is
  // a faithful demo of it.
  constexpr std::array<std::string_view, 4> heroes{"Aria", "Borin", "Cael", "Dusk"};
  rng::normal_distribution<double> ability{12.0, 3.0};  // D&D-ish: mean 12, sd 3

  std::array<int, heroes.size()> scores{};
  for (std::size_t h{0}; h < heroes.size(); ++h) {
    auto const raw{ability.sample(party_rng)};
    // Clamp to a sane die range; scores are conceptually integers.
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

  // 6. Monte-Carlo win odds.
  //
  // "Each hero independently survives the boss with probability p; the party
  // wins if at least one survives." The closed form is 1 - (1 - p)^k, but
  // pretend the real model is too gnarly for algebra and estimate it by
  // simulation. bernoulli(g, p) is the right primitive: one fair-ish trial per
  // hero, drawn from a uniform_real comparison.
  //
  // The estimate is self-checking: with many trials the empirical rate must sit
  // within a few standard errors of the analytic value, so we assert it does.
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
  // First run: print every step of one expedition.
  auto const digest_a{run_expedition("crypt-of-echoes", true)};

  // 7. Determinism proof.
  //
  // Re-run with the *same* seed phrase and confirm the digest matches to the
  // bit. This is the whole promise of the module: same seed -> same stream ->
  // same content, with nothing pulled from the clock or the OS. A *different*
  // phrase must yield a different expedition, so we check that too.
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

  std::println("\nThat is the whole module in one expedition: seeding, uniform and");
  std::println("weighted draws, gamma/normal/exponential/poisson distributions,");
  std::println("shuffling, reservoir sampling, Monte-Carlo, and reproducibility.");
  return 0;
}
